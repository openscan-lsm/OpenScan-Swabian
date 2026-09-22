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
    ConfigureTagger(&tagger, kChannels);
    CHECK(tagger.getConditionalFilterTrigger() ==
          std::vector<channel_t>{kChannels.photonLeading,
                                 kChannels.photonTrailing});
    CHECK(tagger.getConditionalFilterFiltered() ==
          std::vector<channel_t>{kChannels.sync});
}
