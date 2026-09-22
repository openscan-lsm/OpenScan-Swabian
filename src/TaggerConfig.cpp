#include "TaggerConfig.h"

void ConfigureTagger(TimeTaggerBase *tagger, TaggerChannels const &channels,
                     timestamp_t photonDelay_ps, bool &hardwareDelaysApplied) {
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
