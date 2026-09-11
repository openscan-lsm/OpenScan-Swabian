#pragma once

#include <memory>
#include <string>
#include <TimeTagger.h>
#include <OpenScanDeviceLib.h>

struct TimeTagger_PrivateData {
    std::string serial;
    std::unique_ptr<TimeTaggerBase, void(*)(TimeTaggerBase *)> tagger =
        {nullptr, nullptr};
    std::unique_ptr<IteratorBase> pipeline = nullptr;

    int32_t lineClockChannel = 1;
    int32_t syncChannel = 2;
    int32_t photonChannel = 3;

    int32_t syncDelay_ps = 0;
    int32_t lineDelay_ps = 0;
    int32_t maxPhotonPulseWidth_ps = 100'000;
    int32_t maxDiffTime_ps = 15'000;

    bool cumulative = false;
    int32_t histogramBins = 256;

    bool saveHistograms = false;
    bool saveRawData = false;
    std::string fileNamePrefix = "OpenScan-Swabian";
};

inline TimeTagger_PrivateData *GetData(OScDev_Device *device) {
    return static_cast<TimeTagger_PrivateData *>(OScDev_Device_GetImplData(device));
}

OScDev_Error TimeTagger_MakeSettings(OScDev_Device *device, OScDev_PtrArray **settings);