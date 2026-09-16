#include "TagStreamMeasurement.h"

#include <utility>

TagStreamMeasurement::TagStreamMeasurement(
    TimeTaggerBase *tagger, std::vector<channel_t> const &channels,
    PushFunction push)
    : IteratorBase(tagger), push_(std::move(push)) {
    for (auto const channel : channels)
        registerChannel(channel);
    finishInitialization();
}

TagStreamMeasurement::~TagStreamMeasurement() {
    // The pump thread calls next_impl() until stop() has fully returned, and
    // ~IteratorBase() runs only after our members are gone -- so stop here,
    // before push_ is destroyed (see the IteratorBase comment in
    // fake_timetagger/include/TimeTagger.h).
    stop();
}

bool TagStreamMeasurement::next_impl(std::vector<Tag> &incoming_tags,
                                     timestamp_t /*begin_time*/,
                                     timestamp_t /*end_time*/) {
    // TODO: Create and handle TimeReachedEvents for the end_time timestamp
    // (can't hurt to do begin_time as well)
    if (!push_(incoming_tags))
        finish_running();
    return false;
}
