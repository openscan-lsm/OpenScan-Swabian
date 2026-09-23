// Tests for ConfigureTagger against the fake SDK's TimeTagger: that it
// applies the device-side configuration the module relies on.

#include "TaggerConfig.h"

#include <catch2/catch_test_macros.hpp>

#include <TimeTagger.h>

#include <vector>

namespace {
// Negative photon leading edge, as with the module's default Photon Channel;
// trigger levels must still land on the positive (physical) input number.
constexpr TaggerChannels kChannels = {
    .sync = 2,
    .photonLeading = -3,
    .photonTrailing = 3,
    .lineClock = 1,
};
} // namespace

TEST_CASE("ConfigureTagger enables the conditional filter with both photon "
          "edges as trigger and the sync as filtered",
          "[tagger_config]") {
    TimeTagger tagger;
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 0.0, -0.1, 0, applied);
    CHECK(tagger.getConditionalFilterTrigger() ==
          std::vector<channel_t>{kChannels.photonLeading,
                                 kChannels.photonTrailing});
    CHECK(tagger.getConditionalFilterFiltered() ==
          std::vector<channel_t>{kChannels.sync});
}

TEST_CASE("ConfigureTagger sets the sync and photon trigger levels on the "
          "positive channel number of each input",
          "[tagger_config]") {
    TimeTagger tagger;
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 0.25, -0.3, 0, applied);
    CHECK(tagger.getTriggerLevel(2) == 0.25);
    CHECK(tagger.getTriggerLevel(3) == -0.3);
}

TEST_CASE("ConfigureTagger sets the line clock trigger level to the TTL "
          "level clamped to the device's range maximum",
          "[tagger_config]") {
    TimeTagger tagger;
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 0.0, -0.1, 0, applied);
    // The fake's range is -1..1 V; the requested level is 1.5 V.
    CHECK(tagger.getTriggerLevel(1) == 1.0);
}

TEST_CASE("ConfigureTagger applies a nonzero photon delay to both photon "
          "edges and zero to every other channel",
          "[tagger_config]") {
    TimeTagger tagger;
    // Pre-set stale values that must be overwritten.
    tagger.setDelayHardware(kChannels.sync, 111);
    tagger.setDelayHardware(kChannels.lineClock, 222);
    tagger.setDelayHardware(-kChannels.lineClock, 333);
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 0.0, -0.1, 1234, applied);
    CHECK(applied);
    CHECK(tagger.getDelayHardware(kChannels.photonLeading) == 1234);
    CHECK(tagger.getDelayHardware(kChannels.photonTrailing) == 1234);
    CHECK(tagger.getDelayHardware(kChannels.sync) == 0);
    CHECK(tagger.getDelayHardware(kChannels.lineClock) == 0);
    CHECK(tagger.getDelayHardware(-kChannels.lineClock) == 0);
}

TEST_CASE("ConfigureTagger does not touch hardware delays when the delay is "
          "zero and none has been applied",
          "[tagger_config]") {
    TimeTagger tagger;
    tagger.setDelayHardware(kChannels.photonLeading, 555);
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 0.0, -0.1, 0, applied);
    CHECK_FALSE(applied);
    CHECK(tagger.getDelayHardware(kChannels.photonLeading) == 555);
}

TEST_CASE("ConfigureTagger writes a zero delay once a delay has been "
          "applied",
          "[tagger_config]") {
    TimeTagger tagger;
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 0.0, -0.1, 1234, applied);
    REQUIRE(applied);
    ConfigureTagger(&tagger, kChannels, 0.0, -0.1, 0, applied);
    CHECK(applied);
    CHECK(tagger.getDelayHardware(kChannels.photonLeading) == 0);
    CHECK(tagger.getDelayHardware(kChannels.photonTrailing) == 0);
}
