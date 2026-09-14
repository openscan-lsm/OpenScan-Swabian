#pragma once

#include <OpenScanDeviceLib.h>
#include <libtcspc/tcspc.hpp>

#include <memory>

struct TimeTagger_PrivateData;

using abstime_type = tcspc::default_numeric_traits::abstime_type;
using difftime_type = tcspc::default_numeric_traits::difftime_type;
using channel_type = tcspc::default_numeric_traits::channel_type;
using bin_index_type = tcspc::default_numeric_traits::bin_index_type;

struct pixel_start_event {
    tcspc::i64 abstime;
};

struct pixel_stop_event {
    tcspc::i64 abstime;
};

struct pixel_tick_event {
    tcspc::i64 abstime;
};

using TagPipeline =
    tcspc::type_erased_processor<tcspc::type_list<tcspc::swabian_tag_event>>;

inline constexpr char kTagBufferTrackerName[] = "tag_buffer";

TagPipeline MakeProcessingPipeline(TimeTagger_PrivateData *data,
                                   OScDev_Acquisition *acq,
                                   std::shared_ptr<tcspc::context> const &ctx);
