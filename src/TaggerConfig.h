#pragma once

#include <TimeTagger.h>

#include <cstddef>
#include <vector>

struct DetectorConfig {
    std::size_t slot; // DetectorN setting index, for messages
    channel_t leading;
    channel_t trailing;
    double triggerLevel_V;
    timestamp_t hardwareDelay_ps;
};

struct TaggerConfig {
    channel_t sync;
    double syncTriggerLevel_V;
    channel_t lineClock;
    std::vector<DetectorConfig> detectors; // enabled ones only, in slot order
};

// Throws std::runtime_error if there is no detector or if any two of the
// sync, line clock, and detectors share a physical input.
void ValidateTaggerConfig(TaggerConfig const &config);

// Applies the device-side configuration for an acquisition; call before the
// measurement starts. SDK exceptions propagate.
//
// The sync and detector trigger levels are the comparator thresholds for
// their inputs; a trigger level is per physical input, so it covers both
// edges. They are passed to the device as given (not clamped to its range).
// The line clock input gets a fixed TTL/LVTTL-compatible level, clamped to
// the device's range maximum.
//
// Each detector's hardwareDelay_ps is applied to both of its edges; the sync
// and line clock get 0. hardwareDelaysApplied tracks whether this function
// has written hardware delays to the device: they are skipped while it is
// false and every detector delay is 0 (the Time Tagger 20 has no hardware
// delay and would throw; the model is not detected, so this keeps the
// default configuration working on one), and set to true once written. Reset
// it when a device is opened.
void ConfigureTagger(TimeTagger *tagger, TaggerConfig const &config,
                     bool &hardwareDelaysApplied);
