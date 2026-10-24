#include "TimeTaggerPrivate.h"
#include <OpenScanDeviceLib.h>
#include <TimeTagger.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static TimeTagger_PrivateData *GetSettingDeviceData(OScDev_Setting *setting) {
    return static_cast<TimeTagger_PrivateData *>(OScDev_Device_GetImplData(
        (OScDev_Device *)OScDev_Setting_GetImplData(setting)));
}

// Accessors, used as template arguments to the setting classes below, so that
// those can address either a plain member or a member of detectors[D].
template <auto Member> auto &Field(TimeTagger_PrivateData &data) {
    return data.*Member;
}

template <std::size_t D, auto Member>
auto &DetectorField(TimeTagger_PrivateData &data) {
    return data.detectors[D].*Member;
}

template <auto Access> class BoolSetting {
    static OScDev_Error Get(OScDev_Setting *setting, bool *value) {
        *value = Access(*GetSettingDeviceData(setting));
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, bool value) {
        Access(*GetSettingDeviceData(setting)) = value;
        return OScDev_OK;
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetBool = Get,
        .SetBool = Set,
    };
};

// Names is an array of value names, indexed by the enum's underlying value.
template <auto Access, auto const &Names> class EnumSetting {
    using EnumType = std::remove_reference_t<decltype(Access(
        std::declval<TimeTagger_PrivateData &>()))>;

    static OScDev_Error Get(OScDev_Setting *setting, uint32_t *value) {
        *value = static_cast<uint32_t>(Access(*GetSettingDeviceData(setting)));
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, uint32_t value) {
        if (value >= std::size(Names))
            return OScDev_Error_ReturnAsCode(
                OScDev_Error_Create("Enum value out of range"));
        Access(*GetSettingDeviceData(setting)) = static_cast<EnumType>(value);
        return OScDev_OK;
    }
    static OScDev_Error GetNumValues(OScDev_Setting *, uint32_t *count) {
        *count = static_cast<uint32_t>(std::size(Names));
        return OScDev_OK;
    }
    static OScDev_Error GetNameForValue(OScDev_Setting *, uint32_t value,
                                        char *name) {
        if (value >= std::size(Names))
            return OScDev_Error_ReturnAsCode(
                OScDev_Error_Create("Enum value out of range"));
        strncpy_s(name, OScDev_MAX_STR_SIZE, Names[value], _TRUNCATE);
        return OScDev_OK;
    }
    static OScDev_Error GetValueForName(OScDev_Setting *, uint32_t *value,
                                        const char *name) {
        for (std::size_t i = 0; i < std::size(Names); ++i) {
            if (std::strcmp(Names[i], name) == 0) {
                *value = static_cast<uint32_t>(i);
                return OScDev_OK;
            }
        }
        return OScDev_Error_ReturnAsCode(OScDev_Error_Create(
            (std::string("Unknown enum value name: ") + name).c_str()));
    }

  public:
    static inline OScDev_SettingImpl impl = {
        .GetEnum = Get,
        .SetEnum = Set,
        .GetEnumNumValues = GetNumValues,
        .GetEnumNameForValue = GetNameForValue,
        .GetEnumValueForName = GetValueForName,
    };
};

template <auto Access> class ChannelSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = Access(*GetSettingDeviceData(setting));
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        Access(*GetSettingDeviceData(setting)) = value;
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

template <auto Level, auto Channel> class TriggerLevelSetting {
    static OScDev_Error Get(OScDev_Setting *setting, double *value) {
        *value = Level(*GetSettingDeviceData(setting));
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, double value) {
        Level(*GetSettingDeviceData(setting)) = value;
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
                data->tagger->getTriggerLevelRange(std::abs(Channel(*data)));
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

template <auto Channel> struct HardwareDelayRange {
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
            range = data->tagger->getDelayHardwareRange(Channel(*data));
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
template <auto Access, class Range> class NanosecondSetting {
    static OScDev_Error Get(OScDev_Setting *setting, double *value) {
        *value = PsToNs(Access(*GetSettingDeviceData(setting)));
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, double value) {
        Access(*GetSettingDeviceData(setting)) = NsToPs(value);
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

namespace {

using PD = TimeTagger_PrivateData;

constexpr char const *kHistogramFileFormatNames[] = {
    "Raw 16-bit array (.hist)",
    "Compressed 16-bit array (.hist.zst)",
};

constexpr char const *kTagStreamFileFormatNames[] = {
    "Uncompressed (.ttdump)",
    "Compressed (.ttdump.zst)",
};

struct SettingEntry {
    std::string name;
    OScDev_ValueType type;
    OScDev_SettingImpl *impl;
};

template <std::size_t D>
void AppendDetectorSettings(std::vector<SettingEntry> &entries) {
    using DS = DetectorSettings;
    constexpr auto input = &DetectorField<D, &DS::input>;
    std::string const name = "Detector" + std::to_string(D);
    entries.push_back({"Enable" + name, OScDev_ValueType_Bool,
                       &BoolSetting<&DetectorField<D, &DS::enabled>>::impl});
    entries.push_back({name + "Input", OScDev_ValueType_Int32,
                       &ChannelSetting<input>::impl});
    entries.push_back(
        {name + "TriggerLevel_V", OScDev_ValueType_Float64,
         &TriggerLevelSetting<&DetectorField<D, &DS::triggerLevel_V>,
                              input>::impl});
    entries.push_back(
        {name + "HardwareDelay_ns", OScDev_ValueType_Float64,
         &NanosecondSetting<&DetectorField<D, &DS::hardwareDelay_ps>,
                            HardwareDelayRange<input>>::impl});
}

std::vector<SettingEntry> MakeSettingEntries() {
    constexpr auto syncChannel = &Field<&PD::syncChannel>;
    std::vector<SettingEntry> entries = {
        {"SyncInput", OScDev_ValueType_Int32,
         &ChannelSetting<syncChannel>::impl},
        {"LineClockInput", OScDev_ValueType_Int32,
         &ChannelSetting<&Field<&PD::lineClockChannel>>::impl},
        {"SyncTriggerLevel_V", OScDev_ValueType_Float64,
         &TriggerLevelSetting<&Field<&PD::syncTriggerLevel_V>,
                              syncChannel>::impl},
        {"SyncDelay_ns", OScDev_ValueType_Float64,
         &NanosecondSetting<&Field<&PD::syncDelay_ps>, SignedNsRange>::impl},
        {"LineDelay_ns", OScDev_ValueType_Float64,
         &NanosecondSetting<&Field<&PD::lineDelay_ps>,
                            NonNegativeNsRange>::impl},
    };
    [&]<std::size_t... D>(std::index_sequence<D...>) {
        (AppendDetectorSettings<D>(entries), ...);
    }(std::make_index_sequence<kMaxDetectors>());
    entries.insert(
        entries.end(),
        {
            {"MaxPhotonPulseWidth_ns", OScDev_ValueType_Float64,
             &NanosecondSetting<&Field<&PD::maxPhotonPulseWidth_ps>,
                                PositiveNsRange>::impl},
            {"MaxDiffTime_ns", OScDev_ValueType_Float64,
             &NanosecondSetting<&Field<&PD::maxDiffTime_ps>,
                                PositiveNsRange>::impl},
            {"SaveHistograms", OScDev_ValueType_Bool,
             &BoolSetting<&Field<&PD::saveHistograms>>::impl},
            {"HistogramFileFormat", OScDev_ValueType_Enum,
             &EnumSetting<&Field<&PD::histogramFileFormat>,
                          kHistogramFileFormatNames>::impl},
            {"HistogramBins", OScDev_ValueType_Int32,
             &HistogramBinsSetting::impl},
            {"HistogramBinWidth_ps", OScDev_ValueType_Int32,
             &HistogramBinWidthSetting::impl},
            {"IntensityImagesAreCumulative", OScDev_ValueType_Bool,
             &BoolSetting<&Field<&PD::intensityImagesAreCumulative>>::impl},
            {"SaveTagStream", OScDev_ValueType_Bool,
             &BoolSetting<&Field<&PD::saveTagStream>>::impl},
            {"TagStreamFileFormat", OScDev_ValueType_Enum,
             &EnumSetting<&Field<&PD::tagStreamFileFormat>,
                          kTagStreamFileFormatNames>::impl},
            {"FileNamePrefix", OScDev_ValueType_String,
             &FileNamePrefixSetting::impl},
        });
    return entries;
}

} // namespace

OScDev_Error TimeTagger_MakeSettings(OScDev_Device *device,
                                     OScDev_PtrArray **settings) {
    *settings = OScDev_PtrArray_Create();
    for (SettingEntry const &e : MakeSettingEntries()) {
        OScDev_Setting *s;
        OScDev_RichError *err = OScDev_Error_AsRichError(
            OScDev_Setting_Create(&s, e.name.c_str(), e.type, e.impl, device));
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
