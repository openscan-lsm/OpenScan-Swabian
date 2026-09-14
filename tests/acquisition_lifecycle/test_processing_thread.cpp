// Tests for ProcessingThread on a minimal graph: a real_time_buffer<int>
// whose downstream is a test sink that can be made to throw. The test
// thread plays the role of the upstream (the SDK delivery thread).

#include "ProcessingThread.h"

#include <catch2/catch_test_macros.hpp>

#include <libtcspc/tcspc.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

struct SinkState {
    int handled = 0;
    std::function<void(int)> onHandle;
};

class TestSink {
    std::shared_ptr<SinkState> state_;

  public:
    explicit TestSink(std::shared_ptr<SinkState> state)
        : state_(std::move(state)) {}

    void handle(int event) {
        ++state_->handled;
        if (state_->onHandle)
            state_->onHandle(event);
    }
    void flush() {}

    [[nodiscard]] auto introspect_node() const -> tcspc::processor_info {
        return tcspc::processor_info(this, "TestSink");
    }

    [[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph {
        return tcspc::processor_graph().push_entry_point(this);
    }
};

struct FinishedRecord {
    std::mutex mutex;
    int calls = 0;
    std::optional<ProcessingThread::Outcome> outcome;
    std::string message;

    ProcessingThread::FinishedCallback Callback() {
        return [this](ProcessingThread::Outcome o, std::string const &m) {
            std::lock_guard<std::mutex> lock(mutex);
            ++calls;
            outcome = o;
            message = m;
        };
    }
};

auto MakeGraph(std::shared_ptr<tcspc::context> const &ctx,
               std::shared_ptr<SinkState> state) {
    return tcspc::real_time_buffer<int>(
        tcspc::arg::threshold<std::size_t>{1}, std::chrono::milliseconds{10},
        ctx->tracker<tcspc::buffer_accessor>("buf"),
        TestSink(std::move(state)));
}

bool WaitUntilFinished(ProcessingThread const &thread) {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!thread.finished()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

} // namespace

TEST_CASE("ProcessingThread destroyed while idle halts and joins",
          "[lifecycle]") {
    auto ctx = tcspc::context::create();
    auto state = std::make_shared<SinkState>();
    auto graph = MakeGraph(ctx, state);
    FinishedRecord record;
    {
        ProcessingThread thread(ctx->access<tcspc::buffer_accessor>("buf"),
                                record.Callback());
    }
    CHECK(record.calls == 1);
    CHECK(record.outcome == ProcessingThread::Outcome::Halted);
    CHECK(state->handled == 0);
}

TEST_CASE("ProcessingThread explicit halt then destroy invokes callback once",
          "[lifecycle]") {
    auto ctx = tcspc::context::create();
    auto state = std::make_shared<SinkState>();
    auto graph = MakeGraph(ctx, state);
    FinishedRecord record;
    {
        ProcessingThread thread(ctx->access<tcspc::buffer_accessor>("buf"),
                                record.Callback());
        thread.halt();
        REQUIRE(WaitUntilFinished(thread));
        CHECK(record.outcome == ProcessingThread::Outcome::Halted);
    }
    CHECK(record.calls == 1);
}

TEST_CASE("ProcessingThread reports end_of_processing as Completed",
          "[lifecycle]") {
    auto ctx = tcspc::context::create();
    auto state = std::make_shared<SinkState>();
    state->onHandle = [state](int) {
        if (state->handled == 3)
            throw tcspc::end_of_processing("done after three");
    };
    auto graph = MakeGraph(ctx, state);
    FinishedRecord record;
    ProcessingThread thread(ctx->access<tcspc::buffer_accessor>("buf"),
                            record.Callback());

    graph.handle(1);
    graph.handle(2);
    graph.handle(3);
    REQUIRE(WaitUntilFinished(thread));
    CHECK(record.outcome == ProcessingThread::Outcome::Completed);
    CHECK(record.message == "done after three");
    CHECK(state->handled == 3);

    // The buffer bounces the end back to the upstream on its next call.
    CHECK_THROWS_AS(graph.handle(4), tcspc::end_of_processing);
}

TEST_CASE("ProcessingThread reports other exceptions as Failed",
          "[lifecycle]") {
    auto ctx = tcspc::context::create();
    auto state = std::make_shared<SinkState>();
    state->onHandle = [](int) { throw std::runtime_error("bad data"); };
    auto graph = MakeGraph(ctx, state);
    FinishedRecord record;
    ProcessingThread thread(ctx->access<tcspc::buffer_accessor>("buf"),
                            record.Callback());

    graph.handle(1);
    REQUIRE(WaitUntilFinished(thread));
    CHECK(record.outcome == ProcessingThread::Outcome::Failed);
    CHECK(record.message == "bad data");
}

TEST_CASE("ProcessingThread reports upstream flush as Completed",
          "[lifecycle]") {
    auto ctx = tcspc::context::create();
    auto state = std::make_shared<SinkState>();
    auto graph = MakeGraph(ctx, state);
    FinishedRecord record;
    ProcessingThread thread(ctx->access<tcspc::buffer_accessor>("buf"),
                            record.Callback());

    graph.handle(1);
    graph.flush();
    REQUIRE(WaitUntilFinished(thread));
    CHECK(record.outcome == ProcessingThread::Outcome::Completed);
    CHECK(record.message == "processing flushed");
    CHECK(state->handled == 1);
}
