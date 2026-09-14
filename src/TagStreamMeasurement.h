#pragma once

#include <TimeTagger.h>

#include <functional>
#include <vector>

// The SDK measurement: receives raw tag batches on the SDK's delivery thread
// and hands each one to the push function. Delivery starts inside the
// constructor and stops in the destructor.
class TagStreamMeasurement final : public IteratorBase {
  public:
    // Returns false when it wants no more tags; the measurement then calls
    // finish_running() and delivers nothing further. Invoked on the SDK's
    // delivery thread under its measurement lock: must not block on anything
    // that itself waits for this measurement.
    using PushFunction = std::function<bool(std::vector<Tag> const &)>;

    TagStreamMeasurement(TimeTaggerBase *tagger,
                         std::vector<channel_t> const &channels,
                         PushFunction push);
    ~TagStreamMeasurement() override;

  protected:
    bool next_impl(std::vector<Tag> &incoming_tags, timestamp_t begin_time,
                   timestamp_t end_time) override;

  private:
    PushFunction push_;
};
