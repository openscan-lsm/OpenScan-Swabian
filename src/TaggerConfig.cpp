#include "TaggerConfig.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
// TTL/LVTTL-compatible; clamped to the range maximum on models that cannot
// reach it (Time Tagger X: -1 to 1 V).
constexpr double kLineClockTriggerLevel_V = 1.5;
} // namespace

void ValidateTaggerConfig(TaggerConfig const &config) {
    if (config.detectors.empty())
        throw std::runtime_error("No detector is enabled");

    std::vector<std::pair<std::string, channel_t>> inputs = {
        {"Sync", config.sync},
        {"Line clock", config.lineClock},
    };
    for (DetectorConfig const &d : config.detectors)
        inputs.emplace_back("Detector " + std::to_string(d.slot), d.leading);

    for (std::size_t i = 0; i < inputs.size(); ++i) {
        for (std::size_t j = i + 1; j < inputs.size(); ++j) {
            int const input = std::abs(inputs[i].second);
            if (input == std::abs(inputs[j].second))
                throw std::runtime_error(inputs[j].first + " and " +
                                         inputs[i].first + " both use input " +
                                         std::to_string(input));
        }
    }
}

void ConfigureTagger(TimeTagger *tagger, TaggerConfig const &config,
                     bool &hardwareDelaysApplied) {
    // The trigger level belongs to the physical input (positive channel
    // number), so a single call covers both edges.
    auto const setLevel = [&](channel_t channel, double volts) {
        tagger->setTriggerLevel(std::abs(channel), volts);
    };
    setLevel(config.sync, config.syncTriggerLevel_V);
    for (DetectorConfig const &d : config.detectors)
        setLevel(d.leading, d.triggerLevel_V);
    std::vector<double> const range =
        tagger->getTriggerLevelRange(std::abs(config.lineClock));
    if (range.size() != 2)
        throw std::runtime_error(
            "Unexpected trigger level range from Time Tagger");
    setLevel(config.lineClock, std::min(kLineClockTriggerLevel_V, range[1]));

    // Hardware delays are applied on board before the conditional filter, so
    // a detector's delay shifts which sync edge the filter passes for each
    // photon (see README.md, "DetectorNHardwareDelay_ns"). Once a nonzero
    // delay has been written, an all-zero configuration must be written too,
    // since nothing restores the delays at Stop.
    bool const anyDelay = std::any_of(
        config.detectors.begin(), config.detectors.end(),
        [](DetectorConfig const &d) { return d.hardwareDelay_ps != 0; });
    if (anyDelay || hardwareDelaysApplied) {
        for (DetectorConfig const &d : config.detectors) {
            tagger->setDelayHardware(d.leading, d.hardwareDelay_ps);
            tagger->setDelayHardware(d.trailing, d.hardwareDelay_ps);
        }
        tagger->setDelayHardware(config.sync, 0);
        tagger->setDelayHardware(config.lineClock, 0);
        tagger->setDelayHardware(tagger->getInvertedChannel(config.lineClock),
                                 0);
        hardwareDelaysApplied = true;
    }

    // The conditional filter makes the Time Tagger transmit only the first
    // sync edge following each photon edge (of any detector), so the sync
    // channel's bandwidth cost scales with the photon rate rather than the
    // laser rate. Both edges of each detector must trigger because the
    // pipeline correlates the pulse midpoint, which the hardware cannot see.
    // See README.md ("Inputs", "Timing") for the consequences for the
    // SyncDelay_ns and MaxDiffTime_ns settings.
    std::vector<channel_t> triggers;
    for (DetectorConfig const &d : config.detectors) {
        triggers.push_back(d.leading);
        triggers.push_back(d.trailing);
    }
    tagger->setConditionalFilter(triggers, {config.sync});
}
