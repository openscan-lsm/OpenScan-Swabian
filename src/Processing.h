#pragma once

#include <libtcspc/tcspc.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

using TagSpan = std::span<tcspc::swabian_tag_event const>;
using TagPipeline = tcspc::type_erased_processor<
    tcspc::type_list<TagSpan, tcspc::time_reached_event<>>>;

inline constexpr char kTagBufferTrackerName[] = "tag_buffer";

struct ProcessingParams {
    std::uint32_t width;
    std::uint32_t height;
    std::int64_t pixelTime_ps; // abstime units
    std::uint32_t numFrames;

    std::int32_t lineClockChannel;
    std::int32_t syncChannel;
    std::int32_t photonLeadingChannel;
    std::int32_t photonTrailingChannel;

    std::int32_t syncDelay_ps;
    std::int32_t lineDelay_ps;
    std::int32_t maxPhotonPulseWidth_ps;
    std::int32_t maxDiffTime_ps;

    bool cumulative;
    std::int32_t histogramBins;
    std::int32_t histogramBinWidth_ps;

    // Files are written only if the name is set.
    std::optional<std::string> histogramDumpFileName;
    std::optional<std::string> rawDataFileName;
};

// Receives one u16 sample per pixel (width * height), valid only for the
// duration of the call.
using FrameCallback =
    std::function<void(std::uint32_t channel, std::span<tcspc::u16 const>)>;

TagPipeline MakeProcessingPipeline(ProcessingParams const &params,
                                   FrameCallback frameCallback,
                                   std::shared_ptr<tcspc::context> const &ctx);
