#include "Processing.h"

#include <limits>
#include <span>
#include <utility>

namespace {

using abstime_type = tcspc::default_numeric_traits::abstime_type;
using difftime_type = tcspc::default_numeric_traits::difftime_type;
using bin_index_type = tcspc::default_numeric_traits::bin_index_type;

struct pixel_start_event {
    abstime_type abstime;
};

struct pixel_stop_event {
    abstime_type abstime;
};

struct pixel_tick_event {
    abstime_type abstime;
};

struct acquisition_complete_event {
    abstime_type abstime;
};

// Calls the frame callback with a properly-sized width*height buffer (one
// u16 sample per pixel -- the FrameCallback contract has no room for a
// per-pixel histogram cube). This is therefore only ever wired to the
// 1-bin ("intensity") branch of the broadcast in make_processor -- see the
// comment there.
class FrameSink {
    FrameCallback callback_;
    uint32_t channel_;

  public:
    FrameSink(FrameCallback callback, uint32_t channel)
        : callback_(std::move(callback)), channel_(channel) {}

    void handle(tcspc::histogram_array_event<> const &event) {
        callback_(channel_,
                  std::span<tcspc::u16 const>(event.data_bucket.data(),
                                              event.data_bucket.size()));
    }
    void flush() {}

    [[nodiscard]] auto introspect_node() const -> tcspc::processor_info {
        return tcspc::processor_info(this, "FrameSink");
    }

    [[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph {
        return tcspc::processor_graph().push_entry_point(this);
    }
};

// Full per-pixel histogram (histogramBins bins/pixel) -- written to disk as
// raw u16, not sent to the frame callback (which has no way to receive a
// per-pixel histogram cube -- see FrameSink's comment above). The array shape
// is (frames, height, width, bins), or (height, width, bins) if cumulative.
template <bool Cumulative>
auto make_full_histo_proc(ProcessingParams const &params,
                          std::shared_ptr<tcspc::context> const &ctx) {
    using namespace tcspc;
    auto const num_pixels = std::size_t(params.width * params.height);
    auto bsource = recycling_bucket_source<u16>::create();
    auto writer = write_binary_stream(
        binary_file_output_stream(*params.histogramDumpFileName,
                                  arg::truncate{true}),
        recycling_bucket_source<std::byte>::create(),
        // Not flushed if the acquisition is halted, so use a granularity that
        // never leaves part of a frame buffered.
        arg::granularity<>{sizeof(u16)});
    struct reset_event {};
    if constexpr (Cumulative) {
        return append(
            reset_event{}, // Reset before flush to get concluding array.
            scan_histograms<histogram_policy::emit_concluding_events,
                            reset_event>(
                arg::num_elements{num_pixels},
                arg::num_bins{std::size_t(params.histogramBins)},
                arg::max_per_bin<u16>{65535}, bsource,
                count<histogram_array_event<>>(
                    ctx->tracker<count_accessor>("full_frame_counter"),
                    select<type_list<concluding_histogram_array_event<>>>(
                        extract_bucket<concluding_histogram_array_event<>>(
                            view_as_bytes(std::move(writer)))))));
    } else {
        return scan_histograms<histogram_policy::clear_every_scan>(
            arg::num_elements{num_pixels},
            arg::num_bins{std::size_t(params.histogramBins)},
            arg::max_per_bin<u16>{65535}, bsource,
            select<type_list<histogram_array_event<>>>(
                count<histogram_array_event<>>(
                    ctx->tracker<count_accessor>("full_frame_counter"),
                    extract_bucket<histogram_array_event<>>(
                        view_as_bytes(std::move(writer))))));
    }
}

// Single-bin ("intensity") histogram -- one count per pixel, timing
// ignored, sent live to the frame callback via FrameSink. This is the
// branch that gets displayed.
template <bool Cumulative>
auto make_live_histo_proc(ProcessingParams const &params,
                          FrameCallback frameCallback,
                          std::shared_ptr<tcspc::context> const &ctx) {
    using namespace tcspc;
    auto bsource = recycling_bucket_source<u16>::create();
    // scan_histograms emits a histogram_array_event as soon as each frame's
    // scan finishes. clear_every_scan clears the arrays, meaning fresh,
    // non-cumulative frames; the default policy leaves prior counts in place
    // for cumulative frames.
    constexpr histogram_policy policy =
        Cumulative ? histogram_policy::default_policy
                   : histogram_policy::clear_every_scan;
    return scan_histograms<policy>(
        arg::num_elements{std::size_t(params.width * params.height)},
        arg::num_bins{std::size_t(1)}, arg::max_per_bin<u16>{65535}, bsource,
        select<type_list<histogram_array_event<>>>(
            count<histogram_array_event<>>(
                ctx->tracker<count_accessor>("frame_counter"),
                FrameSink(std::move(frameCallback), 0))));
}

template <bool Cumulative>
auto make_processor(ProcessingParams const &params,
                    FrameCallback frameCallback,
                    std::shared_ptr<tcspc::context> const &ctx) {
    using namespace tcspc;

    // clang-format off

    // Photons with difftime >= bin_width * num_bins (possible when that is
    // less than maxDiffTime_ps) are dropped by the full histogram's bin
    // mapper (no clamp), but are still counted in the live image.
    std::int32_t const num_bins = params.histogramBins;
    difftime_type const bin_width = params.histogramBinWidth_ps;

    using tc_event_list = type_list<
        time_correlated_detection_event<>,
        pixel_start_event,
        pixel_stop_event,
        time_reached_event<>>;

    // Single-bin "intensity" equivalent (max_bin_index=0, clamp=true forces
    // every photon into bin 0 regardless of its difftime) -- this is the
    // one actually sent live to the frame callback, since that contract
    // only supports one u16 sample per pixel (see FrameSink's comment).
    // Always built: this is the live image path.
    auto live_pixel_chain =
    map_to_datapoints<time_correlated_detection_event<>>(
        difftime_data_mapper(),
    map_to_bins(
        linear_bin_mapper(
            arg::offset<difftime_type>{0},
            arg::bin_width<difftime_type>{1},
            arg::max_bin_index<bin_index_type>{0},
            arg::clamp{true}),
    cluster_bin_increments<pixel_start_event, pixel_stop_event>(
    count<bin_increment_cluster_event<>>(
        ctx->tracker<count_accessor>("live_pixel_counter"),
    make_live_histo_proc<Cumulative>(params, std::move(frameCallback), ctx)))));

    // The full per-pixel histogram (histogramBins bins/pixel, written to
    // disk) is only useful when a dump file name
    // was given. Broadcasting every time-correlated event
    // to it as well as to live_pixel_chain roughly doubles consumer-side
    // per-event work (map_to_datapoints -> map_to_bins ->
    // cluster_bin_increments -> scan_histograms, all over again, plus the
    // dump itself), for no benefit when nobody's consuming it. So it's
    // built -- and the broadcast exists at all -- only inside this branch;
    // otherwise, tc_downstream is just live_pixel_chain,
    // type-erased to the same interface so both arms of the branch have a
    // common type to hand to merge() below.
    type_erased_processor<tc_event_list> tc_downstream =
        [&]() -> type_erased_processor<tc_event_list> {
        if (!params.histogramDumpFileName)
            return type_erased_processor<tc_event_list>(
                std::move(live_pixel_chain));

        // Full per-pixel TCSPC histogram (histogramBins bins of
        // histogramBinWidth_ps) -- debug-dumped to disk, not sent to the
        // frame callback.
        auto full_pixel_chain =
        map_to_datapoints<time_correlated_detection_event<>>(
            difftime_data_mapper(),
        map_to_bins(
            linear_bin_mapper(
                arg::offset<difftime_type>{0},
                arg::bin_width{bin_width},
                // linear_bin_mapper's own parameter is the index of the
                // last bin (num_bins - 1), not a bin count -- num_bins is
                // guaranteed >= 16 by HistogramBinsSetting's
                // discrete-values list (never includes 0), so this can't
                // underflow.
                arg::max_bin_index<bin_index_type>{bin_index_type(num_bins - 1)}),
        cluster_bin_increments<pixel_start_event, pixel_stop_event>(
        count<bin_increment_cluster_event<>>(
            ctx->tracker<count_accessor>("pixel_counter"),
        make_full_histo_proc<Cumulative>(params, ctx)))));

        return type_erased_processor<tc_event_list>(
            broadcast<tc_event_list>(
                std::move(full_pixel_chain),
                std::move(live_pixel_chain)));
    }();

    // End the acquisition after the requested number of frames. Counted here,
    // downstream of the merge and upstream of any branching, so that every
    // branch has seen the final pixel_stop_event (and thus completed its
    // final frame) before the stop flushes and ends processing. Each
    // pixel_stop_event closes exactly one pixel cluster (start/stop
    // alternation is enforced upstream), so width * height stops is one
    // frame in every scan_histograms.
    auto [tc_merge, start_stop_merge] =
    merge<tc_event_list>(
        arg::max_buffered<>{1 << 20},
    count_up_to<pixel_stop_event, acquisition_complete_event, never_event,
                true>(
        arg::threshold<u64>{u64(params.width) * params.height *
                            params.numFrames},
        arg::limit<u64>{std::numeric_limits<u64>::max()},
        arg::initial_count<u64>{0},
    stop<type_list<acquisition_complete_event>>(
        "reached requested frame count",
    std::move(tc_downstream))));

    auto [sync_merge, cfd_merge] =
    merge<type_list<detection_event<>, time_reached_event<>>>(
        arg::max_buffered<>{1 << 20},
    pair_all_between(
        arg::start_channel{params.syncChannel},
        std::array{params.photonLeadingChannel},
        arg::time_window<abstime_type>{params.maxDiffTime_ps},
    select<type_list<std::array<detection_event<>, 2>, time_reached_event<>>>(
    time_correlate_at_stop(
    std::move(tc_merge)))));

    auto sync_processor =
    delay(arg::delta<abstime_type>{params.syncDelay_ps},
    std::move(sync_merge));

    auto photon_processor =
    pair_one_between(
        arg::start_channel{params.photonLeadingChannel},
        std::array{params.photonTrailingChannel},
        arg::time_window<abstime_type>{params.maxPhotonPulseWidth_ps},
    select<type_list<std::array<detection_event<>, 2>, time_reached_event<>>>(
    // UseStartChannel=true: stamp the emitted pulse event's channel from
    // the start (photonLeadingChannel) side of the pair, not the stop
    // (photonTrailingChannel) side -- downstream (the sync/photon
    // pair_all_between below) matches on photonLeadingChannel, so a pulse
    // event carrying the trailing edge's channel would never correlate
    // with anything.
    time_correlate_at_midpoint<default_numeric_traits, true>(
    remove_time_correlation(
    recover_order<type_list<detection_event<>, time_reached_event<>>>(
        arg::time_window<abstime_type>{params.maxPhotonPulseWidth_ps},
    std::move(cfd_merge))))));

    auto pixel_marker_processor =
    // Convert line clock detection events into (width + 1) pixel tick events
    generate<detection_event<>, pixel_tick_event>(
        linear_timing_generator(
            arg::delay<abstime_type>{params.lineDelay_ps},
            arg::interval<abstime_type>{params.pixelTime_ps},
            arg::count{std::size_t(params.width) + 1}
        ),
    // Convert (width) pixel tick events into (width) "pixel start + pixel stop" intervals
    convert_sequences_to_start_stop<pixel_tick_event, pixel_start_event, pixel_stop_event>(
        arg::count{std::size_t(params.width)},
    // Filter out the line clock events
    select<type_list<pixel_start_event, pixel_stop_event, time_reached_event<>>>(
    // Enforce pixel start/pixel stop alternation
    check_alternating<pixel_start_event, pixel_stop_event>(
    stop_with_error<type_list<warning_event>>(
        "pixel time is such that pixel stop occurs after next pixel start",
    std::move(start_stop_merge))))));

    using tag_bucket = bucket<swabian_tag_event>;
    using bucket_event_list = type_list<tag_bucket>;

    auto unbatched_chain =
    unbatch<tag_bucket>(
    decode_swabian_tags(
    count<detection_event<>>(ctx->tracker<count_accessor>("record_counter"),
    // TODO: On real hardware, a fixed-size circular FIFO means that when
    // software falls behind, old tags get overwritten rather than
    // unboundedly retained -- the device signals this by emitting
    // OverflowBegin/OverflowEnd/MissedEvents tags (Tag::Type in
    // TimeTagger.h), which decode_swabian_tags turns into the
    // begin_lost_interval_event<>/end_lost_interval_event<>/
    // lost_counts_event<> handled right below. IteratorBase::PumpLoop
    // (fake_timetagger/include/TimeTagger.h) currently has no notion of
    // "falling behind" at all -- it unconditionally generates and retains
    // every tag for however much simulated time has elapsed, however long
    // that takes. To model real behavior (and let this exact
    // stop_with_error path actually be exercised, instead of an
    // ever-growing backlog), PumpLoop should detect when it can't keep up
    // (e.g. via something like the reverted MAX_SIMULATED_STEP_PS idea) and
    // emit synthetic overflow tags for the excess span instead of
    // silently retaining or silently dropping it. Also worth reconsidering
    // once that exists: whether stop_with_error (a hard stop) is still the
    // right response to a lost interval, versus something more like a
    // recoverable warning.
    stop_with_error<type_list<
        warning_event,
        begin_lost_interval_event<>,
        end_lost_interval_event<>,
        lost_counts_event<>>>("error in input data",
    check_monotonic(
    stop<type_list<warning_event>>("processing stopped",
    regulate_time_reached(
        arg::interval_threshold<abstime_type>{1 << 30}, // About 1 ms
        arg::count_threshold<>{1 << 18}, // 1/4 of merge buffer size
    route<type_list<detection_event<>>, type_list<time_reached_event<>>>(
        channel_router(std::array{
            std::pair{params.syncChannel, 0},
            std::pair{params.photonLeadingChannel, 1},
            std::pair{params.photonTrailingChannel, 1},
            std::pair{params.lineClockChannel, 2},
        }),
        std::move(sync_processor),
        std::move(photon_processor),
        std::move(pixel_marker_processor)))))))));

    // Save the tags exactly as processed (if a file name was given): 16-byte
    // records in the vendor SDK's Dump format.
    type_erased_processor<bucket_event_list> bucket_downstream =
        [&]() -> type_erased_processor<bucket_event_list> {
        if (!params.rawDataFileName)
            return type_erased_processor<bucket_event_list>(
                std::move(unbatched_chain));

        return type_erased_processor<bucket_event_list>(
            broadcast<bucket_event_list>(
                view_as_bytes(
                write_binary_stream(
                    binary_file_output_stream(*params.rawDataFileName,
                                              arg::truncate{true}),
                    recycling_bucket_source<std::byte>::create(),
                    // Not flushed if the acquisition is halted, so use a
                    // granularity that never leaves bytes buffered.
                    arg::granularity<>{sizeof(swabian_tag_event)})),
                std::move(unbatched_chain)));
    }();

    // Each SDK-delivered batch of tags becomes one bucket.
    return
    copy_to_buckets<TagSpan, swabian_tag_event>(
        recycling_bucket_source<swabian_tag_event>::create(),
    real_time_buffer<tag_bucket>(
        arg::threshold<std::size_t>{2},
        std::chrono::milliseconds{100},
        ctx->tracker<buffer_accessor>(kTagBufferTrackerName),
        std::move(bucket_downstream)));
    // clang-format on
};

} // namespace

TagPipeline
MakeProcessingPipeline(ProcessingParams const &params,
                       FrameCallback frameCallback,
                       std::shared_ptr<tcspc::context> const &ctx) {
    if (params.cumulative)
        return TagPipeline(
            make_processor<true>(params, std::move(frameCallback), ctx));
    return TagPipeline(
        make_processor<false>(params, std::move(frameCallback), ctx));
}
