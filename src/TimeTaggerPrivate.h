#pragma once

#include "AcquisitionRun.h"
#include "Processing.h"
#include "TaggerConfig.h"

#include <OpenScanDeviceLib.h>
#include <TimeTagger.h>
#include <array>
#include <memory>
#include <string>

struct DetectorSettings {
    bool enabled;
    int32_t input;
    double triggerLevel_V;
    int32_t hardwareDelay_ps;
};

struct TimeTagger_PrivateData {
    std::string serial;
    std::unique_ptr<TimeTagger, void (*)(TimeTaggerBase *)> tagger = {nullptr,
                                                                      nullptr};
    std::unique_ptr<AcquisitionRun> run = nullptr;

    int32_t lineClockChannel = 1;
    int32_t syncChannel = 2;

    double syncTriggerLevel_V = 0.0;

    int32_t syncDelay_ps = -12'500;
    int32_t lineDelay_ps = 0;

    std::array<DetectorSettings, kMaxDetectors> detectors = {{
        {true, -3, -0.1, 0},
        {false, -4, -0.1, 0},
        {false, -5, -0.1, 0},
        {false, -6, -0.1, 0},
    }};

    // Whether a hardware delay has been written to the currently open device;
    // see ConfigureTagger.
    bool hardwareDelaysApplied = false;
    int32_t maxPhotonPulseWidth_ps = 100'000;
    int32_t maxDiffTime_ps = 12'500;

    bool intensityImagesAreCumulative = false;
    int32_t histogramBins = 256;
    int32_t histogramBinWidth_ps = 50;

    bool saveHistograms = false;
    HistogramFileFormat histogramFileFormat = HistogramFileFormat::HistZst;
    bool saveTagStream = false;
    TagStreamFileFormat tagStreamFileFormat = TagStreamFileFormat::TTDumpZst;
    std::string fileNamePrefix = "OpenScan-Swabian";
};

inline TimeTagger_PrivateData *GetData(OScDev_Device *device) {
    return static_cast<TimeTagger_PrivateData *>(
        OScDev_Device_GetImplData(device));
}

// Requires an open device (data->tagger).
inline TaggerConfig MakeTaggerConfig(TimeTagger_PrivateData *data) {
    TaggerConfig config{
        .sync = data->syncChannel,
        .syncTriggerLevel_V = data->syncTriggerLevel_V,
        .lineClock = data->lineClockChannel,
        .detectors = {},
    };
    for (std::size_t i = 0; i < data->detectors.size(); ++i) {
        DetectorSettings const &d = data->detectors[i];
        if (!d.enabled)
            continue;
        config.detectors.push_back({
            .slot = i,
            .leading = d.input,
            .trailing = data->tagger->getInvertedChannel(d.input),
            .triggerLevel_V = d.triggerLevel_V,
            .hardwareDelay_ps = d.hardwareDelay_ps,
        });
    }
    return config;
}

OScDev_Error TimeTagger_MakeSettings(OScDev_Device *device,
                                     OScDev_PtrArray **settings);