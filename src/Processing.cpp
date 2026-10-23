#include "Processing.h"

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
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

// Not a valid Time Tagger channel; pads the per-detector-slot arrays.
constexpr std::int32_t kNoChannel = std::numeric_limits<std::int32_t>::min();

using slot_channels = std::array<std::int32_t, kMaxDetectors>;

slot_channels padded_channels(ProcessingParams const &params,
                              std::int32_t DetectorChannels::*member) {
    slot_channels ret;
    ret.fill(kNoChannel);
    for (std::size_t i = 0; i < params.detectors.size(); ++i)
        ret[i] = params.detectors[i].*member;
    return ret;
}

// Routes each detector's events (by leading channel) to its slot index.
auto slot_router(slot_channels const &leading) {
    std::array<std::pair<std::int32_t, std::size_t>, kMaxDetectors> pairs;
    for (std::size_t i = 0; i < kMaxDetectors; ++i)
        pairs[i] = {leading[i], i};
    return tcspc::channel_router(pairs);
}

// Routes sync to 0, both edges of every detector to 1, line clock to 2.
auto raw_tag_router(ProcessingParams const &params,
                    slot_channels const &leading,
                    slot_channels const &trailing) {
    std::array<std::pair<std::int32_t, std::size_t>, 2 + 2 * kMaxDetectors>
        pairs;
    pairs[0] = {params.syncChannel, 0};
    pairs[1] = {params.lineClockChannel, 2};
    for (std::size_t i = 0; i < kMaxDetectors; ++i) {
        pairs[2 + 2 * i] = {leading[i], 1};
        pairs[3 + 2 * i] = {trailing[i], 1};
    }
    return tcspc::channel_router(pairs);
}

template <typename Processor, typename MakeSlot>
auto make_slots(MakeSlot make_slot) {
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<Processor, kMaxDetectors>{make_slot(I)...};
    }(std::make_index_sequence<kMaxDetectors>());
}

// Pairs each detector slot's leading and trailing edges (one pair_one_between
// per slot; padded slots never match).
template <std::size_t I, typename Downstream>
auto pair_pulses(slot_channels const &leading, slot_channels const &trailing,
                 abstime_type window, Downstream downstream) {
    using namespace tcspc;
    if constexpr (I == kMaxDetectors) {
        return downstream;
    } else {
        return pair_one_between(arg::start_channel{leading[I]},
                                std::array{trailing[I]},
                                arg::time_window<abstime_type>{window},
                                pair_pulses<I + 1>(leading, trailing, window,
                                                   std::move(downstream)));
    }
}

// Full per-pixel histogram (histogramBins bins/pixel) -- written to disk as
// raw u16, not sent to the frame callback (which has no way to receive a
// per-pixel histogram cube -- see FrameSink's comment above). Emits the
// array's bytes to downstream.
template <bool Cumulative, typename Downstream>
auto make_full_histo_proc(ProcessingParams const &params,
                          Downstream downstream) {
    using namespace tcspc;
    auto const num_pixels = std::size_t(params.width * params.height);
    auto bsource = recycling_bucket_source<u16>::create();
    struct reset_event {};
    if constexpr (Cumulative) {
        return append(
            reset_event{}, // Reset before flush to get concluding array.
            scan_histograms<histogram_policy::emit_concluding_events,
                            reset_event>(
                arg::num_elements{num_pixels},
                arg::num_bins{std::size_t(params.histogramBins)},
                arg::max_per_bin<u16>{65535}, bsource,
                select<type_list<concluding_histogram_array_event<>>>(
                    extract_bucket<concluding_histogram_array_event<>>(
                        view_as_bytes(std::move(downstream))))));
    } else {
        return scan_histograms<histogram_policy::clear_every_scan>(
            arg::num_elements{num_pixels},
            arg::num_bins{std::size_t(params.histogramBins)},
            arg::max_per_bin<u16>{65535}, bsource,
            select<type_list<histogram_array_event<>>>(
                extract_bucket<histogram_array_event<>>(
                    view_as_bytes(std::move(downstream)))));
    }
}

// Single-bin ("intensity") histogram -- one count per pixel, timing
// ignored, sent live to the frame callback via FrameSink. This is the
// branch that gets displayed.
template <bool Cumulative>
auto make_live_histo_proc(ProcessingParams const &params,
                          FrameCallback frameCallback, std::uint32_t channel) {
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
            FrameSink(std::move(frameCallback), channel)));
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

    using tc_processor = type_erased_processor<tc_event_list>;
    std::size_t const num_detectors = params.detectors.size();
    slot_channels const leading_channels =
        padded_channels(params, &DetectorChannels::leadingChannel);
    slot_channels const trailing_channels =
        padded_channels(params, &DetectorChannels::trailingChannel);

    // Each detector slot gets its own histogramming branch. Time-correlated
    // events carry the detector's leading channel (see photon_processor
    // below); pixel start/stop and time_reached go to every slot.
    auto route_to_slots = [&](std::array<tc_processor, kMaxDetectors> slots) {
        return route_homogeneous<type_list<time_correlated_detection_event<>>>(
            slot_router(leading_channels), std::move(slots));
    };

    // Single-bin "intensity" equivalent (max_bin_index=0, clamp=true forces
    // every photon into bin 0 regardless of its difftime) -- this is the
    // one actually sent live to the frame callback, since that contract
    // only supports one u16 sample per pixel (see FrameSink's comment).
    // Always built: this is the live image path.
    auto live_route = route_to_slots(make_slots<tc_processor>([&](std::size_t i) {
        if (i >= num_detectors)
            return tc_processor(sink_all());
        return tc_processor(
        map_to_datapoints<time_correlated_detection_event<>>(
            difftime_data_mapper(),
        map_to_bins(
            linear_bin_mapper(
                arg::offset<difftime_type>{0},
                arg::bin_width<difftime_type>{1},
                arg::max_bin_index<bin_index_type>{0},
                arg::clamp{true}),
        cluster_bin_increments<pixel_start_event, pixel_stop_event>(
        make_live_histo_proc<Cumulative>(params, frameCallback,
                                         std::uint32_t(i))))));
    }));

    // The full per-pixel histogram (histogramBins bins/pixel, written to
    // disk) is only useful when a dump file name
    // was given. Broadcasting every time-correlated event
    // to it as well as to live_route roughly doubles consumer-side
    // per-event work (map_to_datapoints -> map_to_bins ->
    // cluster_bin_increments -> scan_histograms, all over again, plus the
    // dump itself), for no benefit when nobody's consuming it. So it's
    // built -- and the broadcast exists at all -- only inside this branch;
    // otherwise, tc_downstream is just live_route,
    // type-erased to the same interface so both arms of the branch have a
    // common type to hand to merge() below.
    tc_processor tc_downstream = [&]() -> tc_processor {
        if (!params.histogramDumpFileName)
            return tc_processor(std::move(live_route));

        // All slots write to one file. The layout is (frame, channel, y, x,
        // bin): each slot's scan_histograms completes a frame on that frame's
        // last pixel_stop_event, which the route passes to the slots in slot
        // order. In Cumulative mode the concluding arrays are emitted on
        // flush, also in slot order, giving (channel, y, x, bin).
        auto writer_inputs = merge_n_unsorted<kMaxDetectors>(
            write_binary_stream(
                binary_file_output_stream(*params.histogramDumpFileName,
                                          arg::truncate{true}),
                recycling_bucket_source<std::byte>::create(),
                // Not flushed if the acquisition is halted, so use a
                // granularity that never leaves part of a frame buffered.
                arg::granularity<>{sizeof(u16)}));

        // Full per-pixel TCSPC histogram (histogramBins bins of
        // histogramBinWidth_ps) -- debug-dumped to disk, not sent to the
        // frame callback.
        auto full_route = route_to_slots(make_slots<tc_processor>([&](std::size_t i) {
            // Unused slots still forward flush, so that the writer is
            // flushed once every slot has been.
            if (i >= num_detectors)
                return tc_processor(
                    select<type_list<>>(std::move(writer_inputs[i])));
            return tc_processor(
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
            make_full_histo_proc<Cumulative>(params,
                                             std::move(writer_inputs[i]))))));
        }));

        return tc_processor(
            broadcast<tc_event_list>(
                std::move(full_route),
                std::move(live_route)));
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
        leading_channels,
        arg::time_window<abstime_type>{params.maxDiffTime_ps},
    select<type_list<std::array<detection_event<>, 2>, time_reached_event<>>>(
    time_correlate_at_stop(
    std::move(tc_merge)))));

    auto sync_processor =
    delay(arg::delta<abstime_type>{params.syncDelay_ps},
    std::move(sync_merge));

    auto photon_processor =
    pair_pulses<0>(leading_channels, trailing_channels,
                   params.maxPhotonPulseWidth_ps,
    select<type_list<std::array<detection_event<>, 2>, time_reached_event<>>>(
    // UseStartChannel=true: stamp the emitted pulse event's channel from
    // the start (leading edge) side of the pair, not the stop (trailing
    // edge) side -- downstream (the sync/photon pair_all_between above and
    // the per-detector routing) matches on the leading channel, so a pulse
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
    using buffered_event_list = type_list<tag_bucket, time_reached_event<>>;

    auto unbatched_chain =
    unbatch<tag_bucket>(
    decode_swabian_tags(
    check_monotonic(
    stop_with_error<type_list<
        warning_event,
        begin_lost_interval_event<>,
        end_lost_interval_event<>,
        lost_counts_event<>>>("error in input data",
    regulate_time_reached(
        arg::interval_threshold<abstime_type>{1 << 30}, // About 1 ms
        arg::count_threshold<>{1 << 18}, // 1/4 of merge buffer size
    route<type_list<detection_event<>>, type_list<time_reached_event<>>>(
        raw_tag_router(params, leading_channels, trailing_channels),
        std::move(sync_processor),
        std::move(photon_processor),
        std::move(pixel_marker_processor)))))));

    // Save the tags exactly as processed (if a file name was given): 16-byte
    // records in the vendor SDK's Dump format.
    type_erased_processor<buffered_event_list> bucket_downstream =
        [&]() -> type_erased_processor<buffered_event_list> {
        if (!params.rawDataFileName)
            return type_erased_processor<buffered_event_list>(
                std::move(unbatched_chain));

        return type_erased_processor<buffered_event_list>(
            broadcast<buffered_event_list>(
                // Only the tags go to the raw file (view_as_bytes would
                // otherwise serialize the time_reached_event too).
                select<type_list<tag_bucket>>(
                view_as_bytes(
                write_binary_stream(
                    binary_file_output_stream(*params.rawDataFileName,
                                              arg::truncate{true}),
                    recycling_bucket_source<std::byte>::create(),
                    // Not flushed if the acquisition is halted, so use a
                    // granularity that never leaves bytes buffered.
                    arg::granularity<>{sizeof(swabian_tag_event)}))),
                std::move(unbatched_chain)));
    }();

    // Each SDK-delivered batch of tags becomes one bucket, followed by a
    // time_reached_event; both cross the thread boundary through the buffer.
    return
    copy_to_buckets<TagSpan, swabian_tag_event>(
        recycling_bucket_source<swabian_tag_event>::create(),
    multiplex<buffered_event_list>(
    real_time_buffer<variant_event<buffered_event_list>>(
        arg::threshold<std::size_t>{2},
        std::chrono::milliseconds{100},
        ctx->tracker<buffer_accessor>(kTagBufferTrackerName),
    demultiplex(
        std::move(bucket_downstream)))));
    // clang-format on
};

} // namespace

TagPipeline
MakeProcessingPipeline(ProcessingParams const &params,
                       FrameCallback frameCallback,
                       std::shared_ptr<tcspc::context> const &ctx) {
    if (params.detectors.empty() || params.detectors.size() > kMaxDetectors)
        throw std::invalid_argument("Number of detectors must be 1 to " +
                                    std::to_string(kMaxDetectors));
    if (params.cumulative)
        return TagPipeline(
            make_processor<true>(params, std::move(frameCallback), ctx));
    return TagPipeline(
        make_processor<false>(params, std::move(frameCallback), ctx));
}
