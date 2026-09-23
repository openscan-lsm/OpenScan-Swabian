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
// syncTriggerLevel_V and photonTriggerLevel_V are the comparator thresholds
// for the sync and photon inputs; a trigger level is per physical input, so
// it covers both edges. They are passed to the device as given (not clamped
// to its range). The line clock input gets a fixed TTL/LVTTL-compatible
// level, clamped to the device's range maximum.
//
// photonDelay_ps is the hardware delay for both photon edges; all other
// channels used get 0. hardwareDelaysApplied tracks whether this function has
// written hardware delays to the device: they are skipped while it is false
// and photonDelay_ps is 0 (the Time Tagger 20 has no hardware delay and would
// throw; the model is not detected, so this keeps the default configuration
// working on one), and set to true once written. Reset it when a device is
// opened.
void ConfigureTagger(TimeTagger *tagger, TaggerChannels const &channels,
                     double syncTriggerLevel_V, double photonTriggerLevel_V,
                     timestamp_t photonDelay_ps, bool &hardwareDelaysApplied);
