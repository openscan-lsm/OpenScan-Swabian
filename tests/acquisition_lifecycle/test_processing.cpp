// Tests for the processing pipeline (Processing.cpp) with multiple detectors,
// driven the way AcquisitionRun drives it, with hand-built tags.

#include "Processing.h"
#include "ProcessingThread.h"
#include "TagStreamProcessor.h"
#include "zstd_test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <libtcspc/tcspc.hpp>

#include <TimeTagger.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t kWidth = 2;
constexpr std::uint32_t kHeight = 2;
constexpr std::uint32_t kNumPixels = kWidth * kHeight;
constexpr std::uint32_t kNumFrames = 2;
constexpr std::uint32_t kNumDetectors = 2;
constexpr timestamp_t kPixelTime = 1'000'000;
constexpr timestamp_t kLinePeriod = 3 * kPixelTime;
constexpr std::int32_t kBins = 16;
constexpr std::int32_t kBinWidth = 1000;
constexpr std::int32_t kSyncDelay = -20'000;
constexpr timestamp_t kPulseWidth = 1000;

constexpr channel_t kLineClock = 1;
constexpr channel_t kSync = 2;
// Detector i: leading edge -(3 + i), trailing edge 3 + i.
constexpr channel_t LeadingChannel(std::uint32_t detector) {
    return -(3 + channel_t(detector));
}

ProcessingParams
MakeParams(bool cumulativeIntensity, std::optional<std::string> histogramFile,
           HistogramFileFormat histogramFormat = HistogramFileFormat::Hist) {
    ProcessingParams params{
        .width = kWidth,
        .height = kHeight,
        .pixelTime_ps = kPixelTime,
        .numFrames = kNumFrames,
        .lineClockChannel = kLineClock,
        .syncChannel = kSync,
        .detectors = {},
        .syncDelay_ps = kSyncDelay,
        .lineDelay_ps = 0,
        .maxPhotonPulseWidth_ps = 2000,
        .maxDiffTime_ps = kBins * kBinWidth,
        .intensityImagesAreCumulative = cumulativeIntensity,
        .histogramBins = kBins,
        .histogramBinWidth_ps = kBinWidth,
        .histogramFileName = std::move(histogramFile),
        .histogramFileFormat = histogramFormat,
        .tagStreamFileName = std::nullopt,
        .tagStreamFileFormat = TagStreamFileFormat::TTDump,
    };
    for (std::uint32_t i = 0; i < kNumDetectors; ++i)
        params.detectors.push_back({.leadingChannel = LeadingChannel(i),
                                    .trailingChannel = -LeadingChannel(i)});
    return params;
}

struct Photon {
    std::uint32_t frame;
    std::uint32_t detector;
    std::uint32_t x;
    std::uint32_t y;
    std::int32_t bin;
};

// One line clock rising edge per line, and for each photon a pulse
// (leading, trailing) on its detector plus the following sync edge (as
// passed by the conditional filter), placed so that the difftime after
// applying kSyncDelay falls in the middle of the photon's bin.
std::vector<Tag> MakeTags(std::vector<Photon> const &photons) {
    std::vector<Tag> tags;
    for (std::uint32_t line = 0; line < kNumFrames * kHeight; ++line)
        tags.emplace_back(line * kLinePeriod, kLineClock);
    for (std::size_t n = 0; n < photons.size(); ++n) {
        Photon const &p = photons[n];
        timestamp_t const lineStart = (p.frame * kHeight + p.y) * kLinePeriod;
        // Distinct offsets keep each photon's sync out of other photons'
        // difftime windows.
        timestamp_t const leading =
            lineStart + p.x * kPixelTime + 50'000 * timestamp_t(n + 1);
        timestamp_t const midpoint = leading + kPulseWidth / 2;
        timestamp_t const difftime = p.bin * kBinWidth + kBinWidth / 2;
        tags.emplace_back(leading, LeadingChannel(p.detector));
        tags.emplace_back(leading + kPulseWidth, -LeadingChannel(p.detector));
        tags.emplace_back(midpoint - difftime - kSyncDelay, kSync);
    }
    std::sort(tags.begin(), tags.end(),
              [](Tag const &a, Tag const &b) { return a.time < b.time; });
    return tags;
}

struct RunResult {
    std::optional<ProcessingThread::Outcome> outcome;
    std::map<std::uint32_t, std::vector<std::vector<std::uint16_t>>> frames;
};

bool WaitUntilFinished(ProcessingThread const &thread) {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!thread.finished()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// Returns after the pipeline has been destroyed, so output files are closed.
RunResult Run(ProcessingParams const &params, std::vector<Tag> const &tags) {
    RunResult result;
    std::mutex mutex;
    {
        auto const ctx = tcspc::context::create();
        TagStreamProcessor processor(MakeProcessingPipeline(
            params,
            [&](std::uint32_t channel, std::span<tcspc::u16 const> pixels) {
                std::lock_guard<std::mutex> lock(mutex);
                result.frames[channel].emplace_back(pixels.begin(),
                                                    pixels.end());
            },
            ctx));
        ProcessingThread thread(
            ctx->access<tcspc::buffer_accessor>(kTagBufferTrackerName),
            [&](ProcessingThread::Outcome outcome, std::string const &) {
                std::lock_guard<std::mutex> lock(mutex);
                result.outcome = outcome;
            });
        processor.push(tags, tags.back().time + 1);
        // The merges need a time_reached past the last pixel to emit it.
        // regulate_time_reached only passes one ~1 ms after the previous
        // one, and recover_order holds each until a later one arrives.
        processor.push({}, 10'000'000'000);
        processor.push({}, 20'000'000'000);
        REQUIRE(WaitUntilFinished(thread));
    }
    return result;
}

std::vector<std::uint16_t> ReadU16File(std::filesystem::path const &path,
                                       bool zst) {
    auto const bytes = test_support::ReadMaybeZstdFile(path, zst);
    REQUIRE(bytes.size() % sizeof(std::uint16_t) == 0);
    std::vector<std::uint16_t> data(bytes.size() / sizeof(std::uint16_t));
    std::memcpy(data.data(), bytes.data(), bytes.size());
    return data;
}

std::size_t PixelIndex(Photon const &p) { return p.y * kWidth + p.x; }

std::vector<Photon> const kPhotons = {
    {.frame = 0, .detector = 0, .x = 0, .y = 0, .bin = 3},
    {.frame = 0, .detector = 0, .x = 0, .y = 0, .bin = 3},
    {.frame = 0, .detector = 1, .x = 0, .y = 0, .bin = 7},
    {.frame = 0, .detector = 1, .x = 1, .y = 1, .bin = 15},
    {.frame = 1, .detector = 0, .x = 1, .y = 0, .bin = 0},
    {.frame = 1, .detector = 1, .x = 0, .y = 1, .bin = 9},
    {.frame = 1, .detector = 1, .x = 0, .y = 1, .bin = 3},
    {.frame = 1, .detector = 0, .x = 0, .y = 0, .bin = 3},
};

} // namespace

TEST_CASE("MakeProcessingPipeline rejects an out-of-range detector count",
          "[processing]") {
    auto const ctx = tcspc::context::create();
    auto params = MakeParams(false, std::nullopt);
    auto const callback = [](std::uint32_t, std::span<tcspc::u16 const>) {};

    params.detectors.clear();
    CHECK_THROWS_AS(MakeProcessingPipeline(params, callback, ctx),
                    std::invalid_argument);

    for (std::uint32_t i = 0; i <= kMaxDetectors; ++i)
        params.detectors.push_back({.leadingChannel = LeadingChannel(i),
                                    .trailingChannel = -LeadingChannel(i)});
    CHECK_THROWS_AS(MakeProcessingPipeline(params, callback, ctx),
                    std::invalid_argument);
}

TEST_CASE("Each detector gets its own live frames", "[processing]") {
    bool const cumulativeIntensity = GENERATE(false, true);
    INFO("cumulativeIntensity = " << cumulativeIntensity);
    auto const result =
        Run(MakeParams(cumulativeIntensity, std::nullopt), MakeTags(kPhotons));

    CHECK(result.outcome == ProcessingThread::Outcome::Completed);
    REQUIRE(result.frames.size() == kNumDetectors);
    for (std::uint32_t ch = 0; ch < kNumDetectors; ++ch) {
        INFO("channel " << ch);
        REQUIRE(result.frames.contains(ch));
        auto const &frames = result.frames.at(ch);
        REQUIRE(frames.size() == kNumFrames);
        for (std::uint32_t f = 0; f < kNumFrames; ++f) {
            INFO("frame " << f);
            std::vector<std::uint16_t> expected(kNumPixels);
            for (Photon const &p : kPhotons) {
                if (p.detector == ch &&
                    (p.frame == f || (cumulativeIntensity && p.frame < f)))
                    ++expected[PixelIndex(p)];
            }
            CHECK(frames[f] == expected);
        }
    }
}

TEST_CASE("Saved histogram is the acquisition total, laid out by channel, "
          "pixel, and bin",
          "[processing]") {
    bool const cumulativeIntensity = GENERATE(false, true);
    auto const format =
        GENERATE(HistogramFileFormat::Hist, HistogramFileFormat::HistZst);
    bool const zst = format == HistogramFileFormat::HistZst;
    INFO("cumulativeIntensity = " << cumulativeIntensity);
    INFO("zst = " << zst);
    std::filesystem::path const file =
        std::filesystem::temp_directory_path() /
        (zst ? "openscan-swabian-test-processing.hist.zst"
             : "openscan-swabian-test-processing.hist");
    std::filesystem::remove(file);

    auto const result =
        Run(MakeParams(cumulativeIntensity, file.string(), format),
            MakeTags(kPhotons));
    CHECK(result.outcome == ProcessingThread::Outcome::Completed);

    std::vector<std::uint16_t> expected(kNumDetectors * kNumPixels * kBins);
    for (Photon const &p : kPhotons)
        ++expected[(p.detector * kNumPixels + PixelIndex(p)) * kBins +
                   std::size_t(p.bin)];

    CHECK(ReadU16File(file, zst) == expected);
    std::filesystem::remove(file);
}
