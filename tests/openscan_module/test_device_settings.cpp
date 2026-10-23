// Tests OpenScanSwabian's device-level contract (TimeTagger.cpp,
// TimeTaggerSettings.cpp) through OpenScanLib's real client API:
//
// Any SECTION here that mutates a setting restores it before returning --
// see device_test_support.hpp's Environment comment for why that matters
// (the underlying device object, and therefore its settings, persist for
// the whole test binary).

#include "device_test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace test_support;

TEST_CASE("device identity and role flags", "[device]") {
    LSMFixture fx;

    const char *name = nullptr;
    CheckOk(OSc_Device_GetName(fx.detector, &name), "GetName");
    CHECK(std::string(name).find("Swabian") != std::string::npos);

    const char *displayName = nullptr;
    CheckOk(OSc_Device_GetDisplayName(fx.detector, &displayName),
            "GetDisplayName");
    // "<model>@<name>", per OSc_Device_GetDisplayName's documented format.
    CHECK(std::string(displayName).find('@') != std::string::npos);

    bool hasClock = true, hasScanner = true, hasDetector = false;
    CheckOk(OSc_Device_HasClock(fx.detector, &hasClock), "HasClock");
    CheckOk(OSc_Device_HasScanner(fx.detector, &hasScanner), "HasScanner");
    CheckOk(OSc_Device_HasDetector(fx.detector, &hasDetector), "HasDetector");
    CHECK_FALSE(hasClock);
    CHECK_FALSE(hasScanner);
    CHECK(hasDetector);
}

TEST_CASE("device settings expose the expected names, defaults, and "
          "constraints",
          "[device][settings]") {
    LSMFixture fx;

    OSc_Setting **settings = nullptr;
    size_t count = 0;
    CheckOk(OSc_Device_GetSettings(fx.detector, &settings, &count),
            "GetSettings");
    REQUIRE(count == 16);

    SECTION("SyncInput: default, allowed values, and round-trip") {
        auto *s = FindSetting(settings, count, "SyncInput");
        REQUIRE(s != nullptr);
        int32_t v = 0;
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get");
        CHECK(v == 2);

        // Negative channel numbers select the falling edge. The fake's
        // getChannelList(), sorted.
        int32_t *values = nullptr;
        size_t n = 0;
        CheckOk(OSc_Setting_GetInt32DiscreteValues(s, &values, &n),
                "discrete values");
        std::vector<int32_t> const expected = {-4, -3, -2, -1, 1, 2, 3, 4};
        std::vector<int32_t> const got(values, values + n);
        CHECK(got == expected);

        CheckOk(OSc_Setting_SetInt32Value(s, 4), "set");
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get2");
        CHECK(v == 4);
        CheckOk(OSc_Setting_SetInt32Value(s, -2), "set negative");
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get3");
        CHECK(v == -2);
        CheckOk(OSc_Setting_SetInt32Value(s, 2), "restore"); // leave as found
    }

    SECTION("Detector0Input and LineClockInput defaults") {
        auto *photon = FindSetting(settings, count, "Detector0Input");
        auto *lineClock = FindSetting(settings, count, "LineClockInput");
        REQUIRE(photon != nullptr);
        REQUIRE(lineClock != nullptr);

        int32_t v = 0;
        CheckOk(OSc_Setting_GetInt32Value(photon, &v), "get photon");
        CHECK(v == -3);
        CheckOk(OSc_Setting_GetInt32Value(lineClock, &v), "get line clock");
        CHECK(v == 1);
    }

    SECTION("SyncTriggerLevel_V: default, device range, and round-trip") {
        auto *s = FindSetting(settings, count, "SyncTriggerLevel_V");
        REQUIRE(s != nullptr);
        double v = 1.0;
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get");
        CHECK(v == 0.0);

        // The fake's getTriggerLevelRange().
        double min = 0.0, max = 0.0;
        CheckOk(OSc_Setting_GetFloat64ContinuousRange(s, &min, &max), "range");
        CHECK(min == -1.0);
        CHECK(max == 1.0);

        CheckOk(OSc_Setting_SetFloat64Value(s, 0.35), "set");
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get2");
        CHECK(v == 0.35);
        CheckOk(OSc_Setting_SetFloat64Value(s, 0.0), "restore");
    }

    SECTION("Detector0TriggerLevel_V: default, device range, and "
            "round-trip") {
        auto *s = FindSetting(settings, count, "Detector0TriggerLevel_V");
        REQUIRE(s != nullptr);
        double v = 1.0;
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get");
        CHECK(v == -0.1);

        double min = 0.0, max = 0.0;
        CheckOk(OSc_Setting_GetFloat64ContinuousRange(s, &min, &max), "range");
        CHECK(min == -1.0);
        CHECK(max == 1.0);

        CheckOk(OSc_Setting_SetFloat64Value(s, -0.45), "set");
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get2");
        CHECK(v == -0.45);
        CheckOk(OSc_Setting_SetFloat64Value(s, -0.1), "restore");
    }

    SECTION("SyncDelay_ns: default, round-trip, and rounding to the "
            "nearest picosecond") {
        auto *s = FindSetting(settings, count, "SyncDelay_ns");
        REQUIRE(s != nullptr);
        double v = 0.0;
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get");
        CHECK(v == -12.5);

        CheckOk(OSc_Setting_SetFloat64Value(s, 0.777), "set");
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get2");
        CHECK(v == 0.777);
        CheckOk(OSc_Setting_SetFloat64Value(s, 1.2346), "set unrounded");
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get3");
        CHECK(v == 1.235);
        CheckOk(OSc_Setting_SetFloat64Value(s, -12.5), "restore");
    }

    SECTION("LineDelay_ns: defaults to zero and only allows non-negative "
            "values") {
        auto *s = FindSetting(settings, count, "LineDelay_ns");
        REQUIRE(s != nullptr);
        double v = -1.0;
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get");
        CHECK(v == 0.0);

        double min = -1.0, max = 0.0;
        CheckOk(OSc_Setting_GetFloat64ContinuousRange(s, &min, &max), "range");
        CHECK(min == 0.0);
    }

    SECTION("Detector0HardwareDelay_ns: defaults to zero, round-trips, and "
            "reports the device's hardware delay range") {
        auto *s = FindSetting(settings, count, "Detector0HardwareDelay_ns");
        REQUIRE(s != nullptr);
        double v = -1.0;
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get");
        CHECK(v == 0.0);

        // The fake's getDelayHardwareRange().
        double min = 0.0, max = 0.0;
        CheckOk(OSc_Setting_GetFloat64ContinuousRange(s, &min, &max), "range");
        CHECK(min == -2500.0);
        CHECK(max == 2500.0);

        CheckOk(OSc_Setting_SetFloat64Value(s, -4.0), "set");
        CheckOk(OSc_Setting_GetFloat64Value(s, &v), "get2");
        CHECK(v == -4.0);
        CheckOk(OSc_Setting_SetFloat64Value(s, 0.0), "restore");
    }

    SECTION("MaxPhotonPulseWidth_ns and MaxDiffTime_ns defaults") {
        auto *pulseWidth =
            FindSetting(settings, count, "MaxPhotonPulseWidth_ns");
        auto *diffTime = FindSetting(settings, count, "MaxDiffTime_ns");
        REQUIRE(pulseWidth != nullptr);
        REQUIRE(diffTime != nullptr);

        double v = 0.0;
        CheckOk(OSc_Setting_GetFloat64Value(pulseWidth, &v),
                "get pulse width");
        CHECK(v == 100.0);
        CheckOk(OSc_Setting_GetFloat64Value(diffTime, &v), "get diff time");
        CHECK(v == 12.5);

        double min = -1.0, max = 0.0;
        CheckOk(OSc_Setting_GetFloat64ContinuousRange(pulseWidth, &min, &max),
                "range pulse width");
        CHECK(min == 0.001); // must stay positive
        CheckOk(OSc_Setting_GetFloat64ContinuousRange(diffTime, &min, &max),
                "range diff time");
        CHECK(min == 0.001);
    }

    SECTION("HistogramBins: default and discrete value set") {
        auto *s = FindSetting(settings, count, "HistogramBins");
        REQUIRE(s != nullptr);
        int32_t v = 0;
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get");
        CHECK(v == 256);

        int32_t *values = nullptr;
        size_t n = 0;
        CheckOk(OSc_Setting_GetInt32DiscreteValues(s, &values, &n),
                "discrete values");
        std::vector<int32_t> const expected = {16,  32,   64,   128, 256,
                                               512, 1024, 2048, 4096};
        std::vector<int32_t> const got(values, values + n);
        CHECK(got == expected);
    }

    SECTION("HistogramBinWidth_ps: default, range, and round-trip") {
        auto *s = FindSetting(settings, count, "HistogramBinWidth_ps");
        REQUIRE(s != nullptr);
        int32_t v = 0;
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get");
        CHECK(v == 50);

        int32_t min = 0, max = 0;
        CheckOk(OSc_Setting_GetInt32ContinuousRange(s, &min, &max), "range");
        CHECK(min == 1);
        CHECK(max == 100'000);

        CheckOk(OSc_Setting_SetInt32Value(s, 80), "set");
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get2");
        CHECK(v == 80);
        CheckOk(OSc_Setting_SetInt32Value(s, 50), "restore");
    }

    SECTION("boolean settings default to false and round-trip") {
        for (std::string const &name :
             {"SaveHistograms", "Cumulative", "SaveRawData"}) {
            auto *s = FindSetting(settings, count, name);
            REQUIRE(s != nullptr);
            bool v = true;
            CheckOk(OSc_Setting_GetBoolValue(s, &v), "get " + name);
            CHECK_FALSE(v);

            CheckOk(OSc_Setting_SetBoolValue(s, true), "set " + name);
            CheckOk(OSc_Setting_GetBoolValue(s, &v), "get2 " + name);
            CHECK(v);
            CheckOk(OSc_Setting_SetBoolValue(s, false), "restore " + name);
        }
    }

    SECTION("FileNamePrefix: default and round-trip") {
        auto *s = FindSetting(settings, count, "FileNamePrefix");
        REQUIRE(s != nullptr);
        char buf[OSc_MAX_STR_SIZE];
        CheckOk(OSc_Setting_GetStringValue(s, buf), "get");
        CHECK(std::string(buf) == "OpenScan-Swabian");

        CheckOk(OSc_Setting_SetStringValue(s, "MyPrefix"), "set");
        CheckOk(OSc_Setting_GetStringValue(s, buf), "get2");
        CHECK(std::string(buf) == "MyPrefix");
        CheckOk(OSc_Setting_SetStringValue(s, "OpenScan-Swabian"), "restore");
    }
}
