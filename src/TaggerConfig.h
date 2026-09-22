#pragma once

#include <TimeTagger.h>

struct TaggerChannels {
    channel_t sync;
    channel_t photonLeading;
    channel_t photonTrailing;
    channel_t lineClock;
};

// Applies the device-side configuration for an acquisition; call before the
// measurement starts. SDK exceptions propagate.
//
// photonDelay_ps is the hardware delay for both photon edges; all other
// channels used get 0. hardwareDelaysApplied tracks whether this function has
// written hardware delays to the device: they are skipped while it is false
// and photonDelay_ps is 0 (the Time Tagger 20 has no hardware delay and would
// throw; the model is not detected, so this keeps the default configuration
// working on one), and set to true once written. Reset it when a device is
// opened.
void ConfigureTagger(TimeTaggerBase *tagger, TaggerChannels const &channels,
                     timestamp_t photonDelay_ps, bool &hardwareDelaysApplied);
