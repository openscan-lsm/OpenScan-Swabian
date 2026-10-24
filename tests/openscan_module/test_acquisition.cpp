// End-to-end acquisition test: drives the built OpenScanSwabian.osdev
// module through OpenScanLib's real client API (LSM -> AcqTemplate ->
// Acquisition -> Arm/Start/Wait) exactly as a host application would, and
// checks that a real frame of "reasonable" data comes back.

#include "device_test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// Only for the Tag struct and the fake's channel/period constants, to decode
// the .ttdump file byte-for-byte in the same layout Processing.cpp wrote it
// and to set SyncDelay_ns to the fake's sync period -- this test otherwise
// only drives the module through the public OpenScanLib API, like every
// other test in this directory.
#include <TimeTagger.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <numeric>
#include <string>
#include <vector>

using namespace test_support;

namespace {

// The fake Time Tagger's simulated line-clock period is hardcoded to
// SIMULATED_LINE_WIDTH_PIXELS (512) * pixel period (see
// src/fake_timetagger/include/TimeTagger.h's PumpLoop) -- a different ROI
// here would open pixel windows covering only part of each simulated line,
// silently dropping the rest of that line's photons as "dead time", so
// every acquisition in this file uses this fixed resolution.
constexpr uint32_t kResolution = 512;

// Retains each captured frame, by channel. Assumes each is the same size.
struct FrameCapture {
    std::map<uint32_t, std::vector<std::vector<std::uint16_t>>> frames;
    uint32_t width = 0;
    uint32_t height = 0;
};

bool OnFrame(OSc_Acquisition *, uint32_t channel, void *pixelData,
             void *data) {
    auto *capture = static_cast<FrameCapture *>(data);
    auto const *src = static_cast<std::uint16_t const *>(pixelData);
    capture->frames[channel].emplace_back(
        src, src + static_cast<size_t>(capture->width) * capture->height);
    return true;
}

double Mean(std::vector<std::uint16_t> const &px) {
    return std::accumulate(px.begin(), px.end(), 0.0) /
           static_cast<double>(px.size());
}

void SetBool(OSc_Setting **settings, size_t count, std::string const &name,
             bool value) {
    CheckOk(
        OSc_Setting_SetBoolValue(FindSetting(settings, count, name), value),
        "set " + name);
}

void SetInt32(OSc_Setting **settings, size_t count, std::string const &name,
              int32_t value) {
    CheckOk(
        OSc_Setting_SetInt32Value(FindSetting(settings, count, name), value),
        "set " + name);
}

// Restores the detector settings that tests in this file may change.
void RestoreDetectorSettings(OSc_Setting **settings, size_t count) {
    SetBool(settings, count, "EnableDetector0", true);
    SetBool(settings, count, "EnableDetector1", false);
    SetInt32(settings, count, "Detector1Input", -4);
}

// Everything a test needs to inspect or clean up after RunAcquisition().
// tmpl/acq are left alive (not destroyed) so callers can do their own
// post-wait checks (e.g. OSc_Acquisition_Get*) or call
// OSc_Acquisition_Stop() before tearing them down -- see that function's
// own comment for why Stop() is sometimes required first. settings/
// settingCount are also kept around so callers can restore whatever they
// mutated via additionalSetup (see device_test_support.hpp's Environment
// comment on why that matters).
struct AcquisitionSetup {
    OSc_Setting **settings = nullptr;
    size_t settingCount = 0;
    OSc_AcqTemplate *tmpl = nullptr;
    OSc_Acquisition *acq = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Sets up (without arming) a kResolution x kResolution @ 1MHz OpenScan
// acquisition of numberOfFrames frames, using the Swabian Time Tagger as the
// detector and the fake clock/scanner device as the clock/scanner.
//
// Caller owns cleanup: destroy the returned acq and tmpl when done (via
// OSc_Acquisition_Stop() first if the pipeline needs to be flushed), and
// call RestoreSharedSettings() to undo the settings changed here.
AcquisitionSetup PrepareAcquisition(
    LSMFixture &fx, uint32_t numberOfFrames, bool cumulativeIntensity,
    OSc_FrameCallback callback, void *data,
    std::function<void(OSc_Setting **, size_t)> const &additionalSetup = {}) {
    AcquisitionSetup run;
    CheckOk(
        OSc_Device_GetSettings(fx.detector, &run.settings, &run.settingCount),
        "GetSettings");
    CheckOk(OSc_Setting_SetInt32Value(
                FindSetting(run.settings, run.settingCount, "SyncInput"), 2),
            "set SyncInput");
    CheckOk(
        OSc_Setting_SetInt32Value(
            FindSetting(run.settings, run.settingCount, "Detector0Input"), -3),
        "set Detector0Input");
    SetBool(run.settings, run.settingCount, "EnableDetector0", true);
    SetBool(run.settings, run.settingCount, "EnableDetector1", false);
    CheckOk(
        OSc_Setting_SetInt32Value(
            FindSetting(run.settings, run.settingCount, "LineClockInput"), 1),
        "set LineClockInput");
    CheckOk(
        OSc_Setting_SetBoolValue(FindSetting(run.settings, run.settingCount,
                                             "IntensityImagesAreCumulative"),
                                 cumulativeIntensity),
        "set IntensityImagesAreCumulative");
    // As in real use: the module's conditional filter passes the sync
    // FOLLOWING each photon, so shifting the sync back by one period makes
    // the difftime "time since the preceding sync" (see README.md).
    CheckOk(OSc_Setting_SetFloat64Value(
                FindSetting(run.settings, run.settingCount, "SyncDelay_ns"),
                -SIMULATED_PIXEL_PERIOD_PS / 1000.0),
            "set SyncDelay_ns");
    if (additionalSetup)
        additionalSetup(run.settings, run.settingCount);

    CheckOk(OSc_AcqTemplate_Create(&run.tmpl, fx.lsm), "AcqTemplate_Create");

    OSc_Setting *pixelRateSetting = nullptr;
    CheckOk(OSc_AcqTemplate_GetPixelRateSetting(run.tmpl, &pixelRateSetting),
            "GetPixelRateSetting");
    CheckOk(OSc_Setting_SetFloat64Value(pixelRateSetting, 1e6),
            "set pixel rate");

    OSc_Setting *resolutionSetting = nullptr;
    CheckOk(OSc_AcqTemplate_GetResolutionSetting(run.tmpl, &resolutionSetting),
            "GetResolutionSetting");
    CheckOk(OSc_Setting_SetInt32Value(resolutionSetting,
                                      static_cast<int32_t>(kResolution)),
            "set resolution");

    uint32_t xOff = 0, yOff = 0;
    CheckOk(OSc_AcqTemplate_GetROI(run.tmpl, &xOff, &yOff, &run.width,
                                   &run.height),
            "GetROI");
    CheckOk(OSc_AcqTemplate_SetNumberOfFrames(run.tmpl, numberOfFrames),
            "SetNumberOfFrames");

    CheckOk(OSc_Acquisition_Create(&run.acq, run.tmpl), "Acquisition_Create");

    CheckOk(OSc_Acquisition_SetData(run.acq, data), "SetData");
    CheckOk(OSc_Acquisition_SetFrameCallback(run.acq, callback),
            "SetFrameCallback");

    return run;
}

// PrepareAcquisition(), then arms, starts, and waits for the acquisition.
AcquisitionSetup RunAcquisition(
    LSMFixture &fx, uint32_t numberOfFrames, bool cumulativeIntensity,
    OSc_FrameCallback callback, void *data,
    std::function<void(OSc_Setting **, size_t)> const &additionalSetup = {}) {
    AcquisitionSetup run =
        PrepareAcquisition(fx, numberOfFrames, cumulativeIntensity, callback,
                           data, additionalSetup);
    CheckOk(OSc_Acquisition_Arm(run.acq), "Acquisition_Arm");
    CheckOk(OSc_Acquisition_Start(run.acq), "Acquisition_Start");
    CheckOk(OSc_Acquisition_Wait(run.acq), "Acquisition_Wait");
    return run;
}

// Restores the settings RunAcquisition() itself changes on the shared device
// (see device_test_support.hpp's Environment comment on why that matters).
// Callers restore whatever their own additionalSetup changed.
void RestoreSharedSettings(AcquisitionSetup const &run) {
    CheckOk(
        OSc_Setting_SetBoolValue(FindSetting(run.settings, run.settingCount,
                                             "IntensityImagesAreCumulative"),
                                 false),
        "restore IntensityImagesAreCumulative");
    CheckOk(OSc_Setting_SetFloat64Value(
                FindSetting(run.settings, run.settingCount, "SyncDelay_ns"),
                -12.5),
            "restore SyncDelay_ns");
}

} // namespace

TEST_CASE("a single-frame acquisition produces a plausible intensity image",
          "[acquisition][slow]") {

    LSMFixture fx;
    FrameCapture capture;
    capture.width = kResolution;
    capture.height = kResolution;
    AcquisitionSetup run = RunAcquisition(fx, 1, false, OnFrame, &capture);

    REQUIRE(run.width == kResolution);
    REQUIRE(run.height == kResolution);
    // Assert only one channel in the output
    REQUIRE(capture.frames.size() == 1);
    REQUIRE(capture.frames.contains(0));
    REQUIRE(capture.frames.at(0).size() == 1);
    uint32_t numChannels = 0;
    CheckOk(OSc_Acquisition_GetNumberOfChannels(run.acq, &numChannels),
            "GetNumberOfChannels");
    REQUIRE(numChannels == 1);

    // Assert noise-like qualities on the image
    auto const &px = capture.frames.at(0).front();
    double const mean = Mean(px);
    CHECK(mean > 0);

    double const variance = std::accumulate(px.begin(), px.end(), 0.0,
                                            [mean](double acc, auto v) {
                                                double const d = v - mean;
                                                return acc + d * d;
                                            }) /
                            static_cast<double>(px.size());
    CHECK(std::sqrt(variance) > 0.5);

    // Cleanup
    OSc_Acquisition_Destroy(run.acq);
    OSc_AcqTemplate_Destroy(run.tmpl);
    RestoreSharedSettings(run);
}

TEST_CASE("IntensityImagesAreCumulative delivers one image per frame, each "
          "the running sum of every frame so far",
          "[acquisition][slow]") {
    LSMFixture fx;

    constexpr uint32_t kNumFrames = 3;

    FrameCapture capture;
    capture.width = kResolution;
    capture.height = kResolution;
    AcquisitionSetup run =
        RunAcquisition(fx, kNumFrames, true, OnFrame, &capture);

    REQUIRE(run.width == kResolution);
    REQUIRE(run.height == kResolution);
    REQUIRE(capture.frames.size() == 1);
    auto const &frames = capture.frames.at(0);
    REQUIRE(frames.size() == kNumFrames);

    // Cumulative intensity never resets or subtracts, so every pixel can only
    // grow or stay the same from one delivered frame to the next -- i.e.
    // frame N is frame N-1 plus that frame's own new (random) counts.
    for (size_t f = 1; f < frames.size(); ++f) {
        auto const &prev = frames[f - 1];
        auto const &cur = frames[f];
        REQUIRE(prev.size() == cur.size());
        CHECK(std::equal(prev.begin(), prev.end(), cur.begin(),
                         std::less_equal<>()));
    }

    OSc_Acquisition_Destroy(run.acq);
    OSc_AcqTemplate_Destroy(run.tmpl);
    RestoreSharedSettings(run);
}

TEST_CASE("SaveTagStream writes every registered channel (both photon and "
          "line clock edges, but only the configured sync edge) to a "
          ".ttdump file, with the conditional filter applied to the sync",
          "[acquisition][slow]") {
    LSMFixture fx;

    // Route the tag stream into a scratch directory so we know exactly where
    // to find (and can clean up) the resulting file
    std::filesystem::path const scratch_dir =
        std::filesystem::temp_directory_path() /
        "openscan-swabian-tag-stream-test";
    std::filesystem::remove_all(scratch_dir);
    std::filesystem::create_directories(scratch_dir);
    std::string const prefix = (scratch_dir / "acq").string();

    // This test only inspects the tag stream file, not the delivered image,
    // so the capture just needs to exist as a valid callback target.
    FrameCapture capture;
    capture.width = kResolution;
    capture.height = kResolution;
    AcquisitionSetup run = RunAcquisition(
        fx, 1, false, OnFrame, &capture,
        [&](OSc_Setting **settings, size_t settingCount) {
            CheckOk(OSc_Setting_SetStringValue(
                        FindSetting(settings, settingCount, "FileNamePrefix"),
                        prefix.c_str()),
                    "set FileNamePrefix");
            CheckOk(OSc_Setting_SetBoolValue(
                        FindSetting(settings, settingCount, "SaveTagStream"),
                        true),
                    "set SaveTagStream");
        });

    // Reaching the requested frame count only calls finish_running()
    // (IteratorBase stops polling for more data); it deliberately does NOT
    // flush the pipeline -- see IteratorBase::finish_running()'s own
    // comment in TimeTagger.h. The tag stream file is only closed once
    // something actually stops the acquisition: TimeTagger.cpp's Stop()
    // resets the AcquisitionRun, whose destructor stops the measurement,
    // halts and joins the processing thread, then destroys the graph
    // (closing the file). So this is
    // required here, not just tidiness -- without it the file below is
    // still open when we try to read/delete it.
    CheckOk(OSc_Acquisition_Stop(run.acq), "Acquisition_Stop");

    OSc_Acquisition_Destroy(run.acq);
    OSc_AcqTemplate_Destroy(run.tmpl);

    // UniqueFileName() (src/UniqueFileName.cpp) picks "<prefix>_0000.ttdump"
    // for the first file at a fresh prefix -- this scratch directory was
    // just created above, so that's the only file that can exist here.
    std::filesystem::path const ttdump_file = prefix + "_0000.ttdump";
    REQUIRE(std::filesystem::exists(ttdump_file));

    std::ifstream file(ttdump_file, std::ios::binary);
    REQUIRE(file.good());
    bool sawRisingLineClock = false;
    bool sawFallingLineClock = false;
    bool sawRisingSync = false;
    bool sawFallingSync = false;
    bool sawRisingPhoton = false;
    bool sawFallingPhoton = false;
    // The conditional filter's signature: never two syncs without a photon
    // edge between them (the fake models the filter; see its PumpLoop).
    bool syncPending = false;
    bool sawConsecutiveSyncs = false;
    Tag tag;
    while (file.read(reinterpret_cast<char *>(&tag), sizeof(Tag))) {
        if (tag.channel == LINE_CLOCK_CHANNEL)
            sawRisingLineClock = true;
        else if (tag.channel == -LINE_CLOCK_CHANNEL)
            sawFallingLineClock = true;
        if (tag.channel == SYNC_CHANNEL) {
            sawRisingSync = true;
            if (syncPending)
                sawConsecutiveSyncs = true;
            syncPending = true;
        } else if (tag.channel == -SYNC_CHANNEL)
            sawFallingSync = true;
        if (tag.channel == PHOTON_CHANNELS[0]) {
            sawRisingPhoton = true;
            syncPending = false;
        } else if (tag.channel == -PHOTON_CHANNELS[0]) {
            sawFallingPhoton = true;
            syncPending = false;
        }
    }
    CHECK(sawRisingLineClock);
    CHECK(sawFallingLineClock);
    CHECK(sawRisingSync);
    CHECK_FALSE(sawFallingSync); // the sync's other edge is not registered
    CHECK(sawRisingPhoton);
    CHECK(sawFallingPhoton);
    CHECK_FALSE(sawConsecutiveSyncs);
    file.close();
    std::filesystem::remove_all(scratch_dir);

    // Restore what this test mutated on the shared device (see
    // device_test_support.hpp's Environment comment) -- otherwise these
    // stick around for every later test case in this binary,
    // test_device_settings.cpp's default-value checks included.
    CheckOk(OSc_Setting_SetBoolValue(
                FindSetting(run.settings, run.settingCount, "SaveTagStream"),
                false),
            "restore SaveTagStream");
    CheckOk(OSc_Setting_SetStringValue(
                FindSetting(run.settings, run.settingCount, "FileNamePrefix"),
                "OpenScan-Swabian"),
            "restore FileNamePrefix");
    RestoreSharedSettings(run);
}

namespace {

// Runs a kHistNumFrames acquisition with SaveHistograms enabled and returns
// the contents of the resulting .hist file (the acquisition total), together
// with the expected number of u16 elements per histogram array (height *
// width * bins).
struct HistogramFile {
    std::vector<std::uint16_t> data;
    size_t elementsPerArray = 0;
};

constexpr uint32_t kHistNumFrames = 2;

// With twoDetectors, Detector1 (input -4) is enabled in addition to
// Detector0.
HistogramFile AcquireHistogramFile(bool twoDetectors) {
    LSMFixture fx;

    std::filesystem::path const scratch_dir =
        std::filesystem::temp_directory_path() / "openscan-swabian-hist-test";
    std::filesystem::remove_all(scratch_dir);
    std::filesystem::create_directories(scratch_dir);
    std::string const prefix = (scratch_dir / "acq").string();

    int32_t bins = 0;
    FrameCapture capture;
    capture.width = kResolution;
    capture.height = kResolution;
    AcquisitionSetup run = RunAcquisition(
        fx, kHistNumFrames, false, OnFrame, &capture,
        [&](OSc_Setting **settings, size_t settingCount) {
            CheckOk(OSc_Setting_SetStringValue(
                        FindSetting(settings, settingCount, "FileNamePrefix"),
                        prefix.c_str()),
                    "set FileNamePrefix");
            CheckOk(OSc_Setting_SetBoolValue(
                        FindSetting(settings, settingCount, "SaveHistograms"),
                        true),
                    "set SaveHistograms");
            CheckOk(OSc_Setting_GetInt32Value(
                        FindSetting(settings, settingCount, "HistogramBins"),
                        &bins),
                    "get HistogramBins");
            SetBool(settings, settingCount, "EnableDetector1", twoDetectors);
        });

    // The file stays open until the acquisition is stopped; see the
    // SaveTagStream test above.
    CheckOk(OSc_Acquisition_Stop(run.acq), "Acquisition_Stop");

    OSc_Acquisition_Destroy(run.acq);
    OSc_AcqTemplate_Destroy(run.tmpl);

    HistogramFile result;
    result.elementsPerArray = static_cast<size_t>(run.width) * run.height *
                              static_cast<size_t>(bins);

    std::filesystem::path const hist_file = prefix + "_0000.hist";
    REQUIRE(std::filesystem::exists(hist_file));
    auto const size = std::filesystem::file_size(hist_file);
    REQUIRE(size % sizeof(std::uint16_t) == 0);
    result.data.resize(size / sizeof(std::uint16_t));
    std::ifstream file(hist_file, std::ios::binary);
    REQUIRE(file.read(reinterpret_cast<char *>(result.data.data()),
                      static_cast<std::streamsize>(size)));
    file.close();
    std::filesystem::remove_all(scratch_dir);

    // Restore what this test mutated on the shared device (see
    // device_test_support.hpp's Environment comment).
    CheckOk(OSc_Setting_SetBoolValue(
                FindSetting(run.settings, run.settingCount, "SaveHistograms"),
                false),
            "restore SaveHistograms");
    CheckOk(OSc_Setting_SetStringValue(
                FindSetting(run.settings, run.settingCount, "FileNamePrefix"),
                "OpenScan-Swabian"),
            "restore FileNamePrefix");
    RestoreDetectorSettings(run.settings, run.settingCount);
    RestoreSharedSettings(run);

    return result;
}

std::uint64_t Sum(std::vector<std::uint16_t>::const_iterator begin,
                  std::vector<std::uint16_t>::const_iterator end) {
    return std::accumulate(begin, end, std::uint64_t(0));
}

} // namespace

TEST_CASE("SaveHistograms writes a single histogram array, the sum of all "
          "frames, to a .hist file",
          "[acquisition][slow]") {
    HistogramFile const hist = AcquireHistogramFile(false);
    CHECK(hist.data.size() == hist.elementsPerArray);
    CHECK(Sum(hist.data.begin(), hist.data.end()) > 0);
}

TEST_CASE("SaveHistograms with two detectors writes a histogram array per "
          "detector",
          "[acquisition][slow]") {
    HistogramFile const hist = AcquireHistogramFile(true);
    size_t const numArrays = 2;
    REQUIRE(hist.data.size() == numArrays * hist.elementsPerArray);
    for (size_t i = 0; i < numArrays; ++i) {
        INFO("array " << i);
        auto const begin =
            hist.data.begin() + std::ptrdiff_t(i * hist.elementsPerArray);
        CHECK(Sum(begin, begin + std::ptrdiff_t(hist.elementsPerArray)) > 0);
    }
}

TEST_CASE("two enabled detectors deliver a frame on each of two channels",
          "[acquisition][slow]") {
    LSMFixture fx;
    FrameCapture capture;
    capture.width = kResolution;
    capture.height = kResolution;
    AcquisitionSetup run = RunAcquisition(
        fx, 1, false, OnFrame, &capture,
        [&](OSc_Setting **settings, size_t settingCount) {
            SetBool(settings, settingCount, "EnableDetector1", true);
            SetInt32(settings, settingCount, "Detector1Input", -4);
        });

    uint32_t numChannels = 0;
    CheckOk(OSc_Acquisition_GetNumberOfChannels(run.acq, &numChannels),
            "GetNumberOfChannels");
    CHECK(numChannels == 2);
    CHECK(capture.frames.size() == 2);
    for (uint32_t ch = 0; ch < 2; ++ch) {
        INFO("channel " << ch);
        REQUIRE(capture.frames.contains(ch));
        REQUIRE(capture.frames.at(ch).size() == 1);
        CHECK(Mean(capture.frames.at(ch).front()) > 0);
    }

    OSc_Acquisition_Destroy(run.acq);
    OSc_AcqTemplate_Destroy(run.tmpl);
    RestoreDetectorSettings(run.settings, run.settingCount);
    RestoreSharedSettings(run);
}

TEST_CASE("Arm fails with an invalid detector configuration",
          "[acquisition]") {
    LSMFixture fx;
    FrameCapture capture;
    capture.width = kResolution;
    capture.height = kResolution;

    std::function<void(OSc_Setting **, size_t)> setup;
    SECTION("no detector enabled") {
        setup = [](OSc_Setting **settings, size_t settingCount) {
            SetBool(settings, settingCount, "EnableDetector0", false);
        };
    }
    SECTION("detector on the sync input") {
        setup = [](OSc_Setting **settings, size_t settingCount) {
            SetBool(settings, settingCount, "EnableDetector1", true);
            SetInt32(settings, settingCount, "Detector1Input", 2);
        };
    }

    AcquisitionSetup run =
        PrepareAcquisition(fx, 1, false, OnFrame, &capture, setup);
    OSc_RichError *err = OSc_Acquisition_Arm(run.acq);
    CHECK(err != nullptr);
    if (err)
        OSc_Error_Destroy(err);

    OSc_Acquisition_Destroy(run.acq);
    OSc_AcqTemplate_Destroy(run.tmpl);
    RestoreDetectorSettings(run.settings, run.settingCount);
    RestoreSharedSettings(run);
}
