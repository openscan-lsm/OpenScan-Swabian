#include "TaggerConfig.h"

void ConfigureTagger(TimeTaggerBase *tagger, TaggerChannels const &channels) {
    // The conditional filter makes the Time Tagger transmit only the first
    // sync edge following each photon edge, so the sync channel's bandwidth
    // cost scales with the photon rate rather than the laser rate. Both photon
    // edges must trigger because the pipeline correlates the pulse midpoint,
    // which the hardware cannot see. See README.md ("Channels", "Timing") for
    // the consequences for the Sync Delay and Max Diff Time settings.
    tagger->setConditionalFilter(
        {channels.photonLeading, channels.photonTrailing}, {channels.sync});
}
