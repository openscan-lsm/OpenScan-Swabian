#include "TimeTaggerPrivate.h"
#include <OpenScanDeviceLib.h>
#include <TimeTagger.h>

#include <algorithm>
#include <cmath>
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

namespace {

int32_t NsToPs(double ns) {
    return static_cast<int32_t>(std::clamp<long long>(
        std::llround(ns * 1000.0), std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max()));
}

double PsToNs(int32_t ps) { return ps / 1000.0; }

constexpr double kInt32MinNs = std::numeric_limits<int32_t>::min() / 1000.0;
constexpr double kInt32MaxNs = std::numeric_limits<int32_t>::max() / 1000.0;

struct SignedNsRange {
    static OScDev_Error GetRange(OScDev_Setting *, double *min, double *max) {
        *min = kInt32MinNs;
        *max = kInt32MaxNs;
        return OScDev_OK;
    }
};

struct NonNegativeNsRange {
    static OScDev_Error GetRange(OScDev_Setting *, double *min, double *max) {
        *min = 0.0;
        *max = kInt32MaxNs;
        return OScDev_OK;
    }
};

// Must be positive: used as pair_one_between/pair_all_between time_window.
struct PositiveNsRange {
    static OScDev_Error GetRange(OScDev_Setting *, double *min, double *max) {
        *min = 0.001;
        *max = kInt32MaxNs;
        return OScDev_OK;
    }
};

template <int32_t TimeTagger_PrivateData::*Channel> struct HardwareDelayRange {
    static OScDev_Error GetRange(OScDev_Setting *setting, double *min,
                                 double *max) {
        auto *data = GetSettingDeviceData(setting);
        if (!data->tagger) {
            *min = kInt32MinNs;
            *max = kInt32MaxNs;
            return OScDev_OK;
        }
        std::vector<timestamp_t> range;
        try {
            range = data->tagger->getDelayHardwareRange(data->*Channel);
        } catch (std::exception const &e) {
            return OScDev_Error_ReturnAsCode(OScDev_Error_Create(e.what()));
        }
        if (range.size() != 2) {
            return OScDev_Error_ReturnAsCode(OScDev_Error_Create(
                "Unexpected hardware delay range from Time Tagger"));
        }
        auto const toNs = [](timestamp_t v) {
            return PsToNs(static_cast<int32_t>(
                std::clamp<timestamp_t>(v, std::numeric_limits<int32_t>::min(),
                                        std::numeric_limits<int32_t>::max())));
        };
        *min = toNs(range[0]);
        *max = toNs(range[1]);
        return OScDev_OK;
    }
};

} // namespace

// Stored in picoseconds; exposed in nanoseconds, rounded to the nearest ps.
template <int32_t TimeTagger_PrivateData::*Member, class Range>
class NanosecondSetting {
    static OScDev_Error Get(OScDev_Setting *setting, double *value) {
        *value = PsToNs(GetSettingDeviceData(setting)->*Member);
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, double value) {
        GetSettingDeviceData(setting)->*Member = NsToPs(value);
        return OScDev_OK;
    }
    static OScDev_Error
    GetNumericConstraintType(OScDev_Setting *,
                             OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetFloat64 = Get,
        .SetFloat64 = Set,
        .GetFloat64Range = Range::GetRange,
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
    using PD = TimeTagger_PrivateData;
    struct Entry {
        const char *name;
        OScDev_ValueType type;
        OScDev_SettingImpl *impl;
    };
    Entry const entries[] = {
        {"SyncInput", OScDev_ValueType_Int32,
         &ChannelSetting<&PD::syncChannel>::impl},
        {"LineClockInput", OScDev_ValueType_Int32,
         &ChannelSetting<&PD::lineClockChannel>::impl},
        {"Detector0Input", OScDev_ValueType_Int32,
         &ChannelSetting<&PD::photonChannel>::impl},
        {"SyncTriggerLevel_V", OScDev_ValueType_Float64,
         &TriggerLevelSetting<&PD::syncTriggerLevel_V,
                              &PD::syncChannel>::impl},
        {"Detector0TriggerLevel_V", OScDev_ValueType_Float64,
         &TriggerLevelSetting<&PD::photonTriggerLevel_V,
                              &PD::photonChannel>::impl},
        {"SyncDelay_ns", OScDev_ValueType_Float64,
         &NanosecondSetting<&PD::syncDelay_ps, SignedNsRange>::impl},
        {"LineDelay_ns", OScDev_ValueType_Float64,
         &NanosecondSetting<&PD::lineDelay_ps, NonNegativeNsRange>::impl},
        {"Detector0HardwareDelay_ns", OScDev_ValueType_Float64,
         &NanosecondSetting<&PD::photonDelay_ps,
                            HardwareDelayRange<&PD::photonChannel>>::impl},
        {"MaxPhotonPulseWidth_ns", OScDev_ValueType_Float64,
         &NanosecondSetting<&PD::maxPhotonPulseWidth_ps,
                            PositiveNsRange>::impl},
        {"MaxDiffTime_ns", OScDev_ValueType_Float64,
         &NanosecondSetting<&PD::maxDiffTime_ps, PositiveNsRange>::impl},
        {"SaveHistograms", OScDev_ValueType_Bool,
         &SaveHistogramsSetting::impl},
        {"HistogramBins", OScDev_ValueType_Int32, &HistogramBinsSetting::impl},
        {"HistogramBinWidth_ps", OScDev_ValueType_Int32,
         &HistogramBinWidthSetting::impl},
        {"Cumulative", OScDev_ValueType_Bool, &CumulativeSetting::impl},
        {"SaveRawData", OScDev_ValueType_Bool, &SaveRawDataSetting::impl},
        {"FileNamePrefix", OScDev_ValueType_String,
         &FileNamePrefixSetting::impl},
    };

    *settings = OScDev_PtrArray_Create();
    for (Entry const &e : entries) {
        OScDev_Setting *s;
        OScDev_RichError *err = OScDev_Error_AsRichError(
            OScDev_Setting_Create(&s, e.name, e.type, e.impl, device));
        if (err) {
            for (size_t i = 0; i < OScDev_PtrArray_Size(*settings); ++i) {
                OScDev_Setting_Destroy(
                    (OScDev_Setting *)OScDev_PtrArray_At(*settings, i));
            }
            OScDev_PtrArray_Destroy(*settings);
            *settings = NULL;
            return OScDev_Error_ReturnAsCode(
                OScDev_Error_Wrap(err, "Failed to create settings"));
        }
        OScDev_PtrArray_Append(*settings, s);
    }
    return OScDev_OK;
}
