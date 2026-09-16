#pragma once

#include <TimeTagger.h>
#include <libtcspc/tcspc.hpp>

#include <string>
#include <vector>

// Owns the type-erased processing graph and feeds it raw tags. Once the
// graph signals completion or throws, the processor is closed and accepts
// nothing further.
class TagStreamProcessor {
  public:
    using Pipeline = tcspc::type_erased_processor<
        tcspc::type_list<tcspc::swabian_tag_event>>;
    enum class State { Open, Completed, Failed };

    explicit TagStreamProcessor(Pipeline pipeline);

    // Only ever to be called from one thread (the SDK delivery thread).
    // Returns false once closed; further calls are no-ops that return false
    // without touching the pipeline.
    bool push(std::vector<Tag> const &tags);

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] std::string const &message() const noexcept;

  private:
    Pipeline pipeline_;
    State state_ = State::Open;
    std::string message_;
};
