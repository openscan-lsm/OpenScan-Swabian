#pragma once

#include "AcquisitionRun.h"
#include "TaggerConfig.h"

#include <OpenScanDeviceLib.h>
#include <TimeTagger.h>
#include <memory>
#include <string>

struct TimeTagger_PrivateData {
    std::string serial;
    std::unique_ptr<TimeTaggerBase, void (*)(TimeTaggerBase *)> tagger = {
        nullptr, nullptr};
    std::unique_ptr<AcquisitionRun> run = nullptr;

    int32_t lineClockChannel = 1;
    int32_t syncChannel = 2;
    int32_t photonChannel = -3;

    int32_t syncDelay_ps = 0;
    int32_t lineDelay_ps = 0;
    int32_t photonDelay_ps = 0;
    // Whether a hardware delay has been written to the currently open device;
    // see ConfigureTagger.
    bool hardwareDelaysApplied = false;
    int32_t maxPhotonPulseWidth_ps = 100'000;
    int32_t maxDiffTime_ps = 12'500;

    bool cumulative = false;
    int32_t histogramBins = 256;
    int32_t histogramBinWidth_ps = 50;

    bool saveHistograms = false;
    bool saveRawData = false;
    std::string fileNamePrefix = "OpenScan-Swabian";
};

inline TimeTagger_PrivateData *GetData(OScDev_Device *device) {
    return static_cast<TimeTagger_PrivateData *>(
        OScDev_Device_GetImplData(device));
}

// Requires an open device (data->tagger).
inline TaggerChannels MakeTaggerChannels(TimeTagger_PrivateData *data) {
    return {
        .sync = data->syncChannel,
        .photonLeading = data->photonChannel,
        .photonTrailing =
            data->tagger->getInvertedChannel(data->photonChannel),
        .lineClock = data->lineClockChannel,
    };
}

OScDev_Error TimeTagger_MakeSettings(OScDev_Device *device,
                                     OScDev_PtrArray **settings);