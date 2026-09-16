// Tests for TagStreamProcessor with a trivial graph: a single sink that
// counts swabian_tag_events and can be made to end or fail processing.

#include "TagStreamProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <TimeTagger.h>
#include <libtcspc/tcspc.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

struct SinkState {
    int handled = 0;
    int completeAfter = 0;  // 0: never
    bool failOnFirst = false;
    std::optional<tcspc::swabian_tag_event> last;
};

class TestSink {
    std::shared_ptr<SinkState> state_;

  public:
    explicit TestSink(std::shared_ptr<SinkState> state)
        : state_(std::move(state)) {}

    void handle(tcspc::swabian_tag_event const &event) {
        if (state_->failOnFirst)
            throw std::runtime_error("bad tag");
        ++state_->handled;
        state_->last = event;
        if (state_->handled == state_->completeAfter)
            throw tcspc::end_of_processing("enough tags");
    }
    void flush() {}

    [[nodiscard]] auto introspect_node() const -> tcspc::processor_info {
        return tcspc::processor_info(this, "TestSink");
    }

    [[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph {
        return tcspc::processor_graph().push_entry_point(this);
    }
};

TagStreamProcessor MakeProcessor(std::shared_ptr<SinkState> state) {
    return TagStreamProcessor(
        TagStreamProcessor::Pipeline(TestSink(std::move(state))));
}

std::vector<Tag> MakeTags(int count) {
    std::vector<Tag> tags;
    for (int i = 0; i < count; ++i)
        tags.emplace_back(timestamp_t(1000 * i), LINE_CLOCK_CHANNEL);
    return tags;
}

} // namespace

TEST_CASE("TagStreamProcessor stays open while the graph accepts tags",
          "[lifecycle]") {
    auto state = std::make_shared<SinkState>();
    state->completeAfter = 5;
    auto processor = MakeProcessor(state);

    CHECK(processor.push(MakeTags(3)));
    CHECK(processor.state() == TagStreamProcessor::State::Open);
    CHECK(state->handled == 3);
}

TEST_CASE("TagStreamProcessor closes on end_of_processing", "[lifecycle]") {
    auto state = std::make_shared<SinkState>();
    state->completeAfter = 5;
    auto processor = MakeProcessor(state);

    CHECK_FALSE(processor.push(MakeTags(5)));
    CHECK(processor.state() == TagStreamProcessor::State::Completed);
    CHECK(processor.message() == "enough tags");
    CHECK(state->handled == 5);

    CHECK_FALSE(processor.push(MakeTags(2)));
    CHECK(state->handled == 5);
}

TEST_CASE("TagStreamProcessor closes on error", "[lifecycle]") {
    auto state = std::make_shared<SinkState>();
    state->failOnFirst = true;
    auto processor = MakeProcessor(state);

    CHECK_FALSE(processor.push(MakeTags(1)));
    CHECK(processor.state() == TagStreamProcessor::State::Failed);
    CHECK(processor.message() == "bad tag");
    CHECK(state->handled == 0);
}

TEST_CASE("TagStreamProcessor converts Tag to swabian_tag_event by layout",
          "[lifecycle]") {
    auto state = std::make_shared<SinkState>();
    auto processor = MakeProcessor(state);

    CHECK(processor.push({Tag(123456789LL, 7)}));
    REQUIRE(state->last.has_value());
    CHECK(state->last->type() ==
          tcspc::swabian_tag_event::tag_type::time_tag);
    CHECK(state->last->channel().value() == 7);
    CHECK(state->last->time().value() == 123456789LL);
}
