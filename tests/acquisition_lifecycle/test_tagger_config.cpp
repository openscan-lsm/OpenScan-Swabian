// Tests for ValidateTaggerConfig, and for ConfigureTagger against the fake
// SDK's TimeTagger: that it applies the device-side configuration the module
// relies on.

#include "TaggerConfig.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <TimeTagger.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace {
// Negative photon leading edge, as with the module's default Detector0Input;
// trigger levels must still land on the positive (physical) input number.
DetectorConfig MakeDetector(std::size_t slot, channel_t input,
                            double triggerLevel_V = -0.1,
                            timestamp_t hardwareDelay_ps = 0) {
    return {
        .slot = slot,
        .leading = -input,
        .trailing = input,
        .triggerLevel_V = triggerLevel_V,
        .hardwareDelay_ps = hardwareDelay_ps,
    };
}

TaggerConfig MakeConfig(std::vector<DetectorConfig> detectors = {
                            MakeDetector(0, 3)}) {
    return {
        .sync = 2,
        .syncTriggerLevel_V = 0.0,
        .lineClock = 1,
        .detectors = std::move(detectors),
    };
}
} // namespace

TEST_CASE("ConfigureTagger enables the conditional filter with both photon "
          "edges as trigger and the sync as filtered",
          "[tagger_config]") {
    TimeTagger tagger;
    bool applied = false;
    ConfigureTagger(&tagger, MakeConfig(), applied);
    CHECK(tagger.getConditionalFilterTrigger() ==
          std::vector<channel_t>{-3, 3});
    CHECK(tagger.getConditionalFilterFiltered() == std::vector<channel_t>{2});
}

TEST_CASE("ConfigureTagger uses both edges of every detector as conditional "
          "filter triggers",
          "[tagger_config]") {
    TimeTagger tagger;
    bool applied = false;
    ConfigureTagger(&tagger,
                    MakeConfig({MakeDetector(0, 3), MakeDetector(2, 5)}),
                    applied);
    CHECK(tagger.getConditionalFilterTrigger() ==
          std::vector<channel_t>{-3, 3, -5, 5});
    CHECK(tagger.getConditionalFilterFiltered() == std::vector<channel_t>{2});
}

TEST_CASE("ConfigureTagger sets the sync and detector trigger levels on the "
          "positive channel number of each input",
          "[tagger_config]") {
    TimeTagger tagger;
    bool applied = false;
    auto config =
        MakeConfig({MakeDetector(0, 3, -0.3), MakeDetector(1, 4, -0.4)});
    config.syncTriggerLevel_V = 0.25;
    ConfigureTagger(&tagger, config, applied);
    CHECK(tagger.getTriggerLevel(2) == 0.25);
    CHECK(tagger.getTriggerLevel(3) == -0.3);
    CHECK(tagger.getTriggerLevel(4) == -0.4);
}

TEST_CASE("ConfigureTagger sets the line clock trigger level to the TTL "
          "level clamped to the device's range maximum",
          "[tagger_config]") {
    TimeTagger tagger;
    bool applied = false;
    ConfigureTagger(&tagger, MakeConfig(), applied);
    // The fake's range is -1..1 V; the requested level is 1.5 V.
    CHECK(tagger.getTriggerLevel(1) == 1.0);
}

TEST_CASE("ConfigureTagger applies each detector's delay to both of its "
          "edges and zero to the sync and line clock",
          "[tagger_config]") {
    TimeTagger tagger;
    // Pre-set stale values that must be overwritten.
    tagger.setDelayHardware(2, 111);
    tagger.setDelayHardware(1, 222);
    tagger.setDelayHardware(-1, 333);
    tagger.setDelayHardware(-4, 444);
    bool applied = false;
    ConfigureTagger(&tagger,
                    MakeConfig({MakeDetector(0, 3, -0.1, 1234),
                                MakeDetector(1, 4, -0.1, 0)}),
                    applied);
    CHECK(applied);
    CHECK(tagger.getDelayHardware(-3) == 1234);
    CHECK(tagger.getDelayHardware(3) == 1234);
    CHECK(tagger.getDelayHardware(-4) == 0);
    CHECK(tagger.getDelayHardware(4) == 0);
    CHECK(tagger.getDelayHardware(2) == 0);
    CHECK(tagger.getDelayHardware(1) == 0);
    CHECK(tagger.getDelayHardware(-1) == 0);
}

TEST_CASE("ConfigureTagger does not touch hardware delays when every delay "
          "is zero and none has been applied",
          "[tagger_config]") {
    TimeTagger tagger;
    tagger.setDelayHardware(-3, 555);
    bool applied = false;
    ConfigureTagger(&tagger, MakeConfig(), applied);
    CHECK_FALSE(applied);
    CHECK(tagger.getDelayHardware(-3) == 555);
}

TEST_CASE("ConfigureTagger writes a zero delay once a delay has been "
          "applied",
          "[tagger_config]") {
    TimeTagger tagger;
    bool applied = false;
    ConfigureTagger(&tagger, MakeConfig({MakeDetector(0, 3, -0.1, 1234)}),
                    applied);
    REQUIRE(applied);
    ConfigureTagger(&tagger, MakeConfig(), applied);
    CHECK(applied);
    CHECK(tagger.getDelayHardware(-3) == 0);
    CHECK(tagger.getDelayHardware(3) == 0);
}

TEST_CASE("ValidateTaggerConfig accepts distinct inputs", "[tagger_config]") {
    CHECK_NOTHROW(ValidateTaggerConfig(MakeConfig()));
    CHECK_NOTHROW(ValidateTaggerConfig(MakeConfig(
        {MakeDetector(0, 3), MakeDetector(1, 4), MakeDetector(3, 6)})));
}

TEST_CASE("ValidateTaggerConfig rejects invalid configurations",
          "[tagger_config]") {
    using Catch::Matchers::ContainsSubstring;

    SECTION("no detector") {
        CHECK_THROWS_WITH(ValidateTaggerConfig(MakeConfig({})),
                          ContainsSubstring("No detector"));
    }

    SECTION("two detectors on the same input, opposite edges") {
        auto first = MakeDetector(0, 3);
        auto second = MakeDetector(2, 3);
        std::swap(second.leading, second.trailing);
        CHECK_THROWS_WITH(
            ValidateTaggerConfig(MakeConfig({first, second})),
            ContainsSubstring("Detector 2 and Detector 0 both use input 3"));
    }

    SECTION("detector on the sync input") {
        CHECK_THROWS_WITH(
            ValidateTaggerConfig(
                MakeConfig({MakeDetector(0, 3), MakeDetector(1, 2)})),
            ContainsSubstring("Detector 1 and Sync both use input 2"));
    }

    SECTION("detector on the line clock input") {
        CHECK_THROWS_WITH(
            ValidateTaggerConfig(MakeConfig({MakeDetector(0, 1)})),
            ContainsSubstring("Detector 0 and Line clock both use input 1"));
    }

    SECTION("sync and line clock on the same input") {
        auto config = MakeConfig();
        config.lineClock = -2;
        CHECK_THROWS_WITH(
            ValidateTaggerConfig(config),
            ContainsSubstring("Line clock and Sync both use input 2"));
    }
}
