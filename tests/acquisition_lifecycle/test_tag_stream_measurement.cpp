// Tests for TagStreamMeasurement against the fake SDK's IteratorBase: that
// it delivers what the fake generates for the registered channels, along
// with each block's end_time, and that a push function returning false ends
// delivery.

#include "TagStreamMeasurement.h"

#include <catch2/catch_test_macros.hpp>

#include <TimeTagger.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct Block {
    std::vector<Tag> tags;
    timestamp_t endTime;
};

struct TagCollector {
    std::mutex mutex;
    std::vector<Tag> tags;
    std::vector<Block> blocks;
    int pushCalls = 0;

    TagStreamMeasurement::PushFunction Pusher(bool result = true) {
        return [this, result](std::vector<Tag> const &batch,
                              timestamp_t endTime) {
            std::lock_guard<std::mutex> lock(mutex);
            tags.insert(tags.end(), batch.begin(), batch.end());
            blocks.push_back({batch, endTime});
            ++pushCalls;
            return result;
        };
    }

    [[nodiscard]] std::vector<Tag> Snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return tags;
    }

    [[nodiscard]] std::vector<Block> Blocks() {
        std::lock_guard<std::mutex> lock(mutex);
        return blocks;
    }

    [[nodiscard]] int PushCalls() {
        std::lock_guard<std::mutex> lock(mutex);
        return pushCalls;
    }
};

} // namespace

TEST_CASE("TagStreamMeasurement delivers batches while running",
          "[lifecycle]") {
    TimeTaggerBase tagger;
    TagCollector collector;
    TagStreamMeasurement measurement(&tagger, {LINE_CLOCK_CHANNEL},
                                     collector.Pusher());

    CHECK(measurement.isRunning());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(measurement.isRunning());

    auto const tags = collector.Snapshot();
    CHECK(std::any_of(tags.begin(), tags.end(), [](Tag const &tag) {
        return tag.channel == LINE_CLOCK_CHANNEL;
    }));
}

TEST_CASE("TagStreamMeasurement delivers non-decreasing end times beyond "
          "every tag in the block",
          "[lifecycle]") {
    TimeTaggerBase tagger;
    TagCollector collector;
    TagStreamMeasurement measurement(&tagger, {LINE_CLOCK_CHANNEL},
                                     collector.Pusher());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto const blocks = collector.Blocks();
    REQUIRE(blocks.size() > 1);
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (i > 0)
            CHECK(blocks[i].endTime >= blocks[i - 1].endTime);
        for (auto const &tag : blocks[i].tags)
            CHECK(tag.time < blocks[i].endTime);
    }
}

TEST_CASE("TagStreamMeasurement stops delivery when push returns false",
          "[lifecycle]") {
    TimeTaggerBase tagger;
    TagCollector collector;
    TagStreamMeasurement measurement(&tagger, {LINE_CLOCK_CHANNEL},
                                     collector.Pusher(false));

    CHECK(measurement.waitUntilFinished(1000));
    CHECK_FALSE(measurement.isRunning());
    CHECK(collector.PushCalls() == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(collector.PushCalls() == 1);
}

TEST_CASE("TagStreamMeasurement destructor stops delivery", "[lifecycle]") {
    TimeTaggerBase tagger;
    {
        TagStreamMeasurement measurement(
            &tagger, {LINE_CLOCK_CHANNEL},
            [](std::vector<Tag> const &, timestamp_t) { return true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    SUCCEED("destructor returned");
}

TEST_CASE("TagStreamMeasurement registers exactly the given channels",
          "[lifecycle]") {
    TimeTaggerBase tagger;
    TagCollector collector;
    TagStreamMeasurement measurement(&tagger, {LINE_CLOCK_CHANNEL},
                                     collector.Pusher());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto const tags = collector.Snapshot();
    CHECK_FALSE(tags.empty());
    CHECK(std::all_of(tags.begin(), tags.end(), [](Tag const &tag) {
        return tag.channel == LINE_CLOCK_CHANNEL;
    }));
}
