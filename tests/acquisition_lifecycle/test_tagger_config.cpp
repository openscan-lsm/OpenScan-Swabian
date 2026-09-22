// Tests for ConfigureTagger against the fake SDK's TimeTaggerBase: that it
// applies the device-side configuration the module relies on.

#include "TaggerConfig.h"

#include <catch2/catch_test_macros.hpp>

#include <TimeTagger.h>

#include <vector>

namespace {
constexpr TaggerChannels kChannels = {
    .sync = 2,
    .photonLeading = 3,
    .photonTrailing = -3,
    .lineClock = 1,
};
}

TEST_CASE("ConfigureTagger enables the conditional filter with both photon "
          "edges as trigger and the sync as filtered",
          "[tagger_config]") {
    TimeTaggerBase tagger;
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 0, applied);
    CHECK(tagger.getConditionalFilterTrigger() ==
          std::vector<channel_t>{kChannels.photonLeading,
                                 kChannels.photonTrailing});
    CHECK(tagger.getConditionalFilterFiltered() ==
          std::vector<channel_t>{kChannels.sync});
}

TEST_CASE("ConfigureTagger applies a nonzero photon delay to both photon "
          "edges and zero to every other channel",
          "[tagger_config]") {
    TimeTaggerBase tagger;
    // Pre-set stale values that must be overwritten.
    tagger.setDelayHardware(kChannels.sync, 111);
    tagger.setDelayHardware(kChannels.lineClock, 222);
    tagger.setDelayHardware(-kChannels.lineClock, 333);
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 1234, applied);
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
    TimeTaggerBase tagger;
    tagger.setDelayHardware(kChannels.photonLeading, 555);
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 0, applied);
    CHECK_FALSE(applied);
    CHECK(tagger.getDelayHardware(kChannels.photonLeading) == 555);
}

TEST_CASE("ConfigureTagger writes a zero delay once a delay has been "
          "applied",
          "[tagger_config]") {
    TimeTaggerBase tagger;
    bool applied = false;
    ConfigureTagger(&tagger, kChannels, 1234, applied);
    REQUIRE(applied);
    ConfigureTagger(&tagger, kChannels, 0, applied);
    CHECK(applied);
    CHECK(tagger.getDelayHardware(kChannels.photonLeading) == 0);
    CHECK(tagger.getDelayHardware(kChannels.photonTrailing) == 0);
}
