// Tests for TagStreamMeasurement against the fake SDK's IteratorBase: that
// it delivers what the fake generates for the registered channels, and that
// a push function returning false ends delivery.

#include "TagStreamMeasurement.h"

#include <catch2/catch_test_macros.hpp>

#include <TimeTagger.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct TagCollector {
    std::mutex mutex;
    std::vector<Tag> tags;
    int pushCalls = 0;

    [[nodiscard]] std::vector<Tag> Snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return tags;
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
    TagStreamMeasurement measurement(
        &tagger, {LINE_CLOCK_CHANNEL}, [&](std::vector<Tag> const &batch) {
            std::lock_guard<std::mutex> lock(collector.mutex);
            collector.tags.insert(collector.tags.end(), batch.begin(),
                                  batch.end());
            ++collector.pushCalls;
            return true;
        });

    CHECK(measurement.isRunning());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(measurement.isRunning());

    auto const tags = collector.Snapshot();
    CHECK(std::any_of(tags.begin(), tags.end(), [](Tag const &tag) {
        return tag.channel == LINE_CLOCK_CHANNEL;
    }));
}

TEST_CASE("TagStreamMeasurement stops delivery when push returns false",
          "[lifecycle]") {
    TimeTaggerBase tagger;
    TagCollector collector;
    TagStreamMeasurement measurement(
        &tagger, {LINE_CLOCK_CHANNEL}, [&](std::vector<Tag> const &) {
            std::lock_guard<std::mutex> lock(collector.mutex);
            ++collector.pushCalls;
            return false;
        });

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
            [](std::vector<Tag> const &) { return true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    SUCCEED("destructor returned");
}

TEST_CASE("TagStreamMeasurement registers exactly the given channels",
          "[lifecycle]") {
    TimeTaggerBase tagger;
    TagCollector collector;
    TagStreamMeasurement measurement(
        &tagger, {LINE_CLOCK_CHANNEL}, [&](std::vector<Tag> const &batch) {
            std::lock_guard<std::mutex> lock(collector.mutex);
            collector.tags.insert(collector.tags.end(), batch.begin(),
                                  batch.end());
            return true;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto const tags = collector.Snapshot();
    CHECK_FALSE(tags.empty());
    CHECK(std::all_of(tags.begin(), tags.end(), [](Tag const &tag) {
        return tag.channel == LINE_CLOCK_CHANNEL;
    }));
}
