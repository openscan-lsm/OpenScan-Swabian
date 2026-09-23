#include "TaggerConfig.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace {
// TTL/LVTTL-compatible; clamped to the range maximum on models that cannot
// reach it (Time Tagger X: -1 to 1 V).
constexpr double kLineClockTriggerLevel_V = 1.5;
} // namespace

void ConfigureTagger(TimeTagger *tagger, TaggerChannels const &channels,
                     double syncTriggerLevel_V, double photonTriggerLevel_V,
                     timestamp_t photonDelay_ps, bool &hardwareDelaysApplied) {
    // The trigger level belongs to the physical input (positive channel
    // number), so a single call covers both edges.
    auto const setLevel = [&](channel_t channel, double volts) {
        tagger->setTriggerLevel(std::abs(channel), volts);
    };
    setLevel(channels.sync, syncTriggerLevel_V);
    setLevel(channels.photonLeading, photonTriggerLevel_V);
    std::vector<double> const range =
        tagger->getTriggerLevelRange(std::abs(channels.lineClock));
    if (range.size() != 2)
        throw std::runtime_error(
            "Unexpected trigger level range from Time Tagger");
    setLevel(channels.lineClock, std::min(kLineClockTriggerLevel_V, range[1]));

    // Hardware delays are applied on board before the conditional filter, so
    // the photon delay shifts which sync edge the filter passes for each
    // photon (see README.md, "Photon Delay (ps)"). Once a nonzero delay has
    // been written, an all-zero configuration must be written too, since
    // nothing restores the delays at Stop.
    if (photonDelay_ps != 0 || hardwareDelaysApplied) {
        tagger->setDelayHardware(channels.photonLeading, photonDelay_ps);
        tagger->setDelayHardware(channels.photonTrailing, photonDelay_ps);
        tagger->setDelayHardware(channels.sync, 0);
        tagger->setDelayHardware(channels.lineClock, 0);
        tagger->setDelayHardware(
            tagger->getInvertedChannel(channels.lineClock), 0);
        hardwareDelaysApplied = true;
    }

    // The conditional filter makes the Time Tagger transmit only the first
    // sync edge following each photon edge, so the sync channel's bandwidth
    // cost scales with the photon rate rather than the laser rate. Both photon
    // edges must trigger because the pipeline correlates the pulse midpoint,
    // which the hardware cannot see. See README.md ("Channels", "Timing") for
    // the consequences for the Sync Delay and Max Diff Time settings.
    tagger->setConditionalFilter(
        {channels.photonLeading, channels.photonTrailing}, {channels.sync});
}
