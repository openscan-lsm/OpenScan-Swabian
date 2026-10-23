#include "Processing.h"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string_view>

namespace {

constexpr char kUsage[] =
    "Usage: dump_graph [--cumulative] [--histogram-dump] [--raw-data]\n"
    "                  [--detectors N]\n"
    "\n"
    "Print the libtcspc processing graph in Graphviz dot format.\n"
    "\n"
    "  --cumulative      cumulative (rather than per-frame) histograms\n"
    "  --histogram-dump  include the histogram dump file output\n"
    "  --raw-data        include the raw data file output\n"
    "  --detectors N     number of detectors, 1-4 (default 1)\n";

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
        .cumulative = false,
        .histogramBins = 256,
        .histogramBinWidth_ps = 50,
        .histogramDumpFileName = {},
        .rawDataFileName = {},
    };
    int numDetectors = 1;

    // The file outputs open their files on construction; NUL discards.
    for (int i = 1; i < argc; ++i) {
        std::string_view const arg = argv[i];
        if (arg == "--cumulative") {
            params.cumulative = true;
        } else if (arg == "--histogram-dump") {
            params.histogramDumpFileName = "NUL";
        } else if (arg == "--raw-data") {
            params.rawDataFileName = "NUL";
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
            .leadingChannel = -(3 + i),
            .trailingChannel = 3 + i,
        });

    try {
        auto const pipeline = MakeProcessingPipeline(
            params, [](std::uint32_t, std::span<tcspc::u16 const>) {},
            tcspc::context::create());
        std::cout << tcspc::graphviz_from_processor_graph(
            pipeline.introspect_graph());
    } catch (std::exception const &exc) {
        std::cerr << exc.what() << '\n';
        return 1;
    }
    return 0;
}
