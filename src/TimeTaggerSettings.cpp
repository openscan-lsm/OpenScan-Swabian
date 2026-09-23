#include "TimeTaggerPrivate.h"
#include <OpenScanDeviceLib.h>
#include <TimeTagger.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <vector>

static TimeTagger_PrivateData *GetSettingDeviceData(OScDev_Setting *setting) {
    return static_cast<TimeTagger_PrivateData *>(OScDev_Device_GetImplData(
        (OScDev_Device *)OScDev_Setting_GetImplData(setting)));
}

template <int32_t TimeTagger_PrivateData::*Member> class ChannelSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->*Member;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->*Member = value;
        return OScDev_OK;
    }
    static OScDev_Error
    GetNumericConstraintType(OScDev_Setting *,
                             OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_DiscreteValues;
        return OScDev_OK;
    }
    static OScDev_Error GetDiscreteValues(OScDev_Setting *setting,
                                          OScDev_NumArray **values) {
        auto *data = GetSettingDeviceData(setting);
        if (!data->tagger) {
            return OScDev_Error_ReturnAsCode(
                OScDev_Error_Create("Time Tagger is not open"));
        }
        // Positive = rising edge, negative = falling edge, as in the SDK.
        std::vector<channel_t> channels;
        try {
            channels = data->tagger->getChannelList(ChannelEdge::All);
        } catch (std::exception const &e) {
            return OScDev_Error_ReturnAsCode(OScDev_Error_Create(e.what()));
        }
        std::sort(channels.begin(), channels.end());
        *values = OScDev_NumArray_Create();
        for (channel_t ch : channels)
            OScDev_NumArray_Append(*values, ch);
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32DiscreteValues = GetDiscreteValues,
    };
};

template <double TimeTagger_PrivateData::*Level,
          int32_t TimeTagger_PrivateData::*Channel>
class TriggerLevelSetting {
    static OScDev_Error Get(OScDev_Setting *setting, double *value) {
        *value = GetSettingDeviceData(setting)->*Level;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, double value) {
        GetSettingDeviceData(setting)->*Level = value;
        return OScDev_OK;
    }
    static OScDev_Error
    GetNumericConstraintType(OScDev_Setting *,
                             OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *setting, double *min,
                                 double *max) {
        auto *data = GetSettingDeviceData(setting);
        if (!data->tagger) {
            // Widest range of any known model (Time Tagger Ultra).
            *min = -2.5;
            *max = 2.5;
            return OScDev_OK;
        }
        std::vector<double> range;
        try {
            range =
                data->tagger->getTriggerLevelRange(std::abs(data->*Channel));
        } catch (std::exception const &e) {
            return OScDev_Error_ReturnAsCode(OScDev_Error_Create(e.what()));
        }
        if (range.size() != 2) {
            return OScDev_Error_ReturnAsCode(OScDev_Error_Create(
                "Unexpected trigger level range from Time Tagger"));
        }
        *min = range[0];
        *max = range[1];
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetFloat64 = Get,
        .SetFloat64 = Set,
        .GetFloat64Range = GetRange,
    };
};

class SyncDelaySetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->syncDelay_ps;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->syncDelay_ps = value;
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetInt32 = Get,
        .SetInt32 = Set,
    };
};

class LineDelaySetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->lineDelay_ps;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->lineDelay_ps = value;
        return OScDev_OK;
    }
    static OScDev_Error
    GetNumericConstraintType(OScDev_Setting *,
                             OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *, int32_t *min,
                                 int32_t *max) {
        // Line delay should be non-negative integers
        *min = 0;
        *max = std::numeric_limits<int32_t>::max();
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32Range = GetRange,
    };
};

class PhotonDelaySetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->photonDelay_ps;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->photonDelay_ps = value;
        return OScDev_OK;
    }
    static OScDev_Error
    GetNumericConstraintType(OScDev_Setting *,
                             OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *setting, int32_t *min,
                                 int32_t *max) {
        auto *data = GetSettingDeviceData(setting);
        if (!data->tagger) {
            *min = std::numeric_limits<int32_t>::min();
            *max = std::numeric_limits<int32_t>::max();
            return OScDev_OK;
        }
        std::vector<timestamp_t> range;
        try {
            range = data->tagger->getDelayHardwareRange(data->photonChannel);
        } catch (std::exception const &e) {
            return OScDev_Error_ReturnAsCode(OScDev_Error_Create(e.what()));
        }
        if (range.size() != 2) {
            return OScDev_Error_ReturnAsCode(OScDev_Error_Create(
                "Unexpected hardware delay range from Time Tagger"));
        }
        auto const clamp = [](timestamp_t v) {
            return static_cast<int32_t>(
                std::clamp<timestamp_t>(v, std::numeric_limits<int32_t>::min(),
                                        std::numeric_limits<int32_t>::max()));
        };
        *min = clamp(range[0]);
        *max = clamp(range[1]);
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32Range = GetRange,
    };
};

class MaxPhotonPulseWidthSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->maxPhotonPulseWidth_ps;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->maxPhotonPulseWidth_ps = value;
        return OScDev_OK;
    }
    static OScDev_Error
    GetNumericConstraintType(OScDev_Setting *,
                             OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *, int32_t *min,
                                 int32_t *max) {
        // Widths should be positive integers
        *min = 1;
        *max = std::numeric_limits<int32_t>::max();
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32Range = GetRange,
    };
};

class MaxDiffTimeSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->maxDiffTime_ps;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->maxDiffTime_ps = value;
        return OScDev_OK;
    }
    static OScDev_Error
    GetNumericConstraintType(OScDev_Setting *,
                             OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *, int32_t *min,
                                 int32_t *max) {
        // Must be positive: it's used as the pair_all_between time_window
        // in Processing.cpp.
        *min = 1;
        *max = std::numeric_limits<int32_t>::max();
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32Range = GetRange,
    };
};

class SaveHistogramsSetting {
    static OScDev_Error Get(OScDev_Setting *setting, bool *value) {
        *value = GetSettingDeviceData(setting)->saveHistograms;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, bool value) {
        GetSettingDeviceData(setting)->saveHistograms = value;
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetBool = Get,
        .SetBool = Set,
    };
};

class HistogramBinsSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->histogramBins;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->histogramBins = value;
        return OScDev_OK;
    }
    static OScDev_Error
    GetNumericConstraintType(OScDev_Setting *,
                             OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_DiscreteValues;
        return OScDev_OK;
    }
    static OScDev_Error GetDiscreteValues(OScDev_Setting *,
                                          OScDev_NumArray **values) {
        // A discrete range is enforced, but purely for convenience
        // (i.e. no reason I know of that we couldn't widen it later).
        static const int32_t valuesArray[] = {
            16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
        };
        *values = OScDev_NumArray_Create();
        for (int32_t v : valuesArray) {
            OScDev_NumArray_Append(*values, v);
        }
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32DiscreteValues = GetDiscreteValues,
    };
};

class HistogramBinWidthSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->histogramBinWidth_ps;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->histogramBinWidth_ps = value;
        return OScDev_OK;
    }
    static OScDev_Error
    GetNumericConstraintType(OScDev_Setting *,
                             OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *, int32_t *min,
                                 int32_t *max) {
        // Max is 100 ns, well below the ~524 ns at which the histogram range
        // would overflow the int32 difftime at 4096 bins.
        *min = 1;
        *max = 100'000;
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32Range = GetRange,
    };
};

class CumulativeSetting {
    static OScDev_Error Get(OScDev_Setting *setting, bool *value) {
        *value = GetSettingDeviceData(setting)->cumulative;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, bool value) {
        GetSettingDeviceData(setting)->cumulative = value;
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetBool = Get,
        .SetBool = Set,
    };
};

class SaveRawDataSetting {
    static OScDev_Error Get(OScDev_Setting *setting, bool *value) {
        *value = GetSettingDeviceData(setting)->saveRawData;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, bool value) {
        GetSettingDeviceData(setting)->saveRawData = value;
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetBool = Get,
        .SetBool = Set,
    };
};

class FileNamePrefixSetting {
    static OScDev_Error Get(OScDev_Setting *setting, char *value) {
        strncpy_s(value, OScDev_MAX_STR_SIZE,
                  GetSettingDeviceData(setting)->fileNamePrefix.c_str(),
                  _TRUNCATE);
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, const char *value) {
        GetSettingDeviceData(setting)->fileNamePrefix = value;
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetString = Get,
        .SetString = Set,
    };
};

OScDev_Error TimeTagger_MakeSettings(OScDev_Device *device,
                                     OScDev_PtrArray **settings) {
    OScDev_RichError *err = OScDev_RichError_OK;
    *settings = OScDev_PtrArray_Create();

    OScDev_Setting *s;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s, "Sync Channel", OScDev_ValueType_Int32,
        &ChannelSetting<&TimeTagger_PrivateData::syncChannel>::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s, "Photon Channel", OScDev_ValueType_Int32,
        &ChannelSetting<&TimeTagger_PrivateData::photonChannel>::impl,
        device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s, "Line Clock Channel", OScDev_ValueType_Int32,
        &ChannelSetting<&TimeTagger_PrivateData::lineClockChannel>::impl,
        device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s, "Sync Trigger Level (V)", OScDev_ValueType_Float64,
        &TriggerLevelSetting<&TimeTagger_PrivateData::syncTriggerLevel_V,
                             &TimeTagger_PrivateData::syncChannel>::impl,
        device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s, "Photon Trigger Level (V)", OScDev_ValueType_Float64,
        &TriggerLevelSetting<&TimeTagger_PrivateData::photonTriggerLevel_V,
                             &TimeTagger_PrivateData::photonChannel>::impl,
        device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(
        OScDev_Setting_Create(&s, "Sync Delay (ps)", OScDev_ValueType_Int32,
                              &SyncDelaySetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(
        OScDev_Setting_Create(&s, "Line Delay (ps)", OScDev_ValueType_Int32,
                              &LineDelaySetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(
        OScDev_Setting_Create(&s, "Photon Delay (ps)", OScDev_ValueType_Int32,
                              &PhotonDelaySetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s, "Max Photon Pulse Width (ps)", OScDev_ValueType_Int32,
        &MaxPhotonPulseWidthSetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(
        OScDev_Setting_Create(&s, "Max Diff Time (ps)", OScDev_ValueType_Int32,
                              &MaxDiffTimeSetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(
        OScDev_Setting_Create(&s, "Save Histograms", OScDev_ValueType_Bool,
                              &SaveHistogramsSetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(
        OScDev_Setting_Create(&s, "Histogram Bins", OScDev_ValueType_Int32,
                              &HistogramBinsSetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s, "Histogram Bin Width (ps)", OScDev_ValueType_Int32,
        &HistogramBinWidthSetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(
        OScDev_Setting_Create(&s, "Cumulative", OScDev_ValueType_Bool,
                              &CumulativeSetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(
        OScDev_Setting_Create(&s, "Save Raw Data", OScDev_ValueType_Bool,
                              &SaveRawDataSetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(
        OScDev_Setting_Create(&s, "File Name Prefix", OScDev_ValueType_String,
                              &FileNamePrefixSetting::impl, device));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    return OScDev_OK;
error:
    for (size_t i = 0; i < OScDev_PtrArray_Size(*settings); ++i) {
        OScDev_Setting_Destroy(
            (OScDev_Setting *)OScDev_PtrArray_At(*settings, i));
    }
    OScDev_PtrArray_Destroy(*settings);
    *settings = NULL;
    return OScDev_Error_ReturnAsCode(
        OScDev_Error_Wrap(err, "Failed to create settings"));
}
