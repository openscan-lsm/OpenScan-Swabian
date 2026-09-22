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
void ConfigureTagger(TimeTaggerBase *tagger, TaggerChannels const &channels);
