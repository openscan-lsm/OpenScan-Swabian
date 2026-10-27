#include "CountRates.h"
#include "Processing.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>

namespace {

constexpr char kUsage[] =
    "Usage: dump_graph [--cumulative-intensity] [--histogram-file]\n"
    "                  [--detectors N]\n"
    "\n"
    "Print the libtcspc processing graph in Graphviz dot format.\n"
    "\n"
    "  --cumulative-intensity  cumulative (rather than per-frame) intensity\n"
    "                          images\n"
    "  --histogram-file        include the histogram file output\n"
    "  --detectors N           number of detectors, 1-4 (default 1)\n";

} // namespace

int main(int argc, char *argv[]) {
    ProcessingParams params{
        .width = 4,
        .height = 4,
        .pixelTime_ps = 1000,
        .numFrames = 1,
        .lineClockChannel = 1,
        .syncChannel = 2,
        .detectors = {},
        .syncDelay_ps = 0,
        .lineDelay_ps = 0,
        .maxPhotonPulseWidth_ps = 1000,
        .maxDiffTime_ps = 12500,
        .intensityImagesAreCumulative = false,
        .histogramBins = 256,
        .histogramBinWidth_ps = 50,
        .histogramFileName = {},
        .histogramFileFormat = HistogramFileFormat::HistZst,
        .tagStreamFileName = {},
        .tagStreamFileFormat = TagStreamFileFormat::TTDumpZst,
        .countRateTimeConstant_ps = 100'000'000'000,
    };
    int numDetectors = 1;

    // The histogram file output opens its file on construction; NUL discards.
    for (int i = 1; i < argc; ++i) {
        std::string_view const arg = argv[i];
        if (arg == "--cumulative-intensity") {
            params.intensityImagesAreCumulative = true;
        } else if (arg == "--histogram-file") {
            params.histogramFileName = "NUL";
        } else if (arg == "--detectors" && i + 1 < argc) {
            std::string_view const value = argv[++i];
            auto const [end, ec] = std::from_chars(
                value.data(), value.data() + value.size(), numDetectors);
            if (ec != std::errc() || end != value.data() + value.size() ||
                numDetectors < 1 || numDetectors > int(kMaxDetectors)) {
                std::cerr << "Invalid number of detectors: " << value << "\n\n"
                          << kUsage;
                return 2;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << kUsage;
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n\n" << kUsage;
            return 2;
        }
    }

    for (int i = 0; i < numDetectors; ++i)
        params.detectors.push_back({
            .slot = std::size_t(i),
            .leadingChannel = -(3 + i),
            .trailingChannel = 3 + i,
        });

    try {
        auto const pipeline = MakeProcessingPipeline(
            params, [](std::uint32_t, std::span<tcspc::u16 const>) {},
            std::make_shared<CountRates>(), tcspc::context::create());
        std::cout << tcspc::graphviz_from_processor_graph(
            pipeline.introspect_graph());
    } catch (std::exception const &exc) {
        std::cerr << exc.what() << '\n';
        return 1;
    }
    return 0;
}
