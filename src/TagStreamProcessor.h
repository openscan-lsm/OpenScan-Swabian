#pragma once

#include <TimeTagger.h>
#include <libtcspc/tcspc.hpp>

#include <span>
#include <string>
#include <vector>

// Owns the type-erased processing graph and feeds it raw tags. Once the
// graph signals completion or throws, the processor is closed and accepts
// nothing further.
class TagStreamProcessor {
  public:
    using TagSpan = std::span<tcspc::swabian_tag_event const>;
    using Pipeline = tcspc::type_erased_processor<
        tcspc::type_list<TagSpan, tcspc::time_reached_event<>>>;
    enum class State { Open, Completed, Failed };

    explicit TagStreamProcessor(Pipeline pipeline);

    // Feeds the block's tags (if any) followed by a time_reached_event
    // derived from end_time (the SDK's exclusive end of the block).
    // Only ever to be called from one thread (the SDK delivery thread).
    // Returns false once closed; further calls are no-ops that return false
    // without touching the pipeline.
    bool push(std::vector<Tag> const &tags, timestamp_t end_time);

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] std::string const &message() const noexcept;

  private:
    Pipeline pipeline_;
    State state_ = State::Open;
    std::string message_;
};
