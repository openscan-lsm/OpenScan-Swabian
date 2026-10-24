#include "ZstdOutputStream.h"
#include "zstd_test_support.hpp"

#include <libtcspc/tcspc.hpp>

#include <catch2/catch_test_macros.hpp>

#include <zstd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using test_support::ZstdDecompress;

namespace {

class RecordingStream {
    std::shared_ptr<std::vector<std::byte>> bytes_;
    bool fail_;

  public:
    explicit RecordingStream(std::shared_ptr<std::vector<std::byte>> bytes,
                             bool fail = false)
        : bytes_(std::move(bytes)), fail_(fail) {}

    auto is_error() noexcept -> bool { return fail_; }
    auto tell() noexcept -> std::optional<std::uint64_t> {
        return bytes_->size();
    }
    void write(std::span<std::byte const> buffer) noexcept {
        bytes_->insert(bytes_->end(), buffer.begin(), buffer.end());
    }
};

static_assert(tcspc::output_stream<ZstdOutputStream<RecordingStream>>);

std::vector<std::byte> Pattern(std::size_t size, unsigned seed) {
    std::vector<std::byte> v(size);
    for (std::size_t i = 0; i < size; ++i)
        v[i] = static_cast<std::byte>((i * 31 + seed) % 251);
    return v;
}

} // namespace

TEST_CASE("ZstdOutputStream round-trips writes of varied sizes",
          "[ZstdOutputStream]") {
    auto compressed = std::make_shared<std::vector<std::byte>>();
    std::vector<std::byte> expected;
    {
        ZstdOutputStream stream(RecordingStream(compressed), 3);
        for (std::size_t size :
             {std::size_t(0), std::size_t(1), std::size_t(16),
              std::size_t(1000), 3 * ZSTD_CStreamInSize() + 7}) {
            auto const chunk = Pattern(size, unsigned(size));
            stream.write(chunk);
            expected.insert(expected.end(), chunk.begin(), chunk.end());
        }
        CHECK_FALSE(stream.is_error());
        CHECK_FALSE(stream.tell().has_value());
    }
    CHECK(ZstdDecompress(*compressed) == expected);
}

TEST_CASE("ZstdOutputStream with no writes produces an empty frame",
          "[ZstdOutputStream]") {
    auto compressed = std::make_shared<std::vector<std::byte>>();
    {
        ZstdOutputStream stream(RecordingStream(compressed), 3);
    }
    CHECK_FALSE(compressed->empty());
    CHECK(ZstdDecompress(*compressed).empty());
}

TEST_CASE("Moved-from ZstdOutputStream writes nothing on destruction",
          "[ZstdOutputStream]") {
    auto fromBytes = std::make_shared<std::vector<std::byte>>();
    auto compressed = std::make_shared<std::vector<std::byte>>();
    auto const data = Pattern(5000, 1);
    {
        ZstdOutputStream<RecordingStream> source(RecordingStream(fromBytes),
                                                 3);
        {
            ZstdOutputStream<RecordingStream> dest(std::move(source));
            dest.write(data);
        }
        // Both streams wrap the same recorder; take what dest produced.
        *compressed = *fromBytes;
    }
    CHECK(*fromBytes == *compressed);
    CHECK(ZstdDecompress(*compressed) == data);
}

TEST_CASE("ZstdOutputStream reports errors of the wrapped stream",
          "[ZstdOutputStream]") {
    auto compressed = std::make_shared<std::vector<std::byte>>();
    ZstdOutputStream stream(RecordingStream(compressed, true), 3);
    CHECK(stream.is_error());
    stream.write(Pattern(100, 0));
    CHECK(stream.is_error());
}
