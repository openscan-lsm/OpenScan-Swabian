#pragma once

#include <zstd.h>

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace test_support {

// Decompresses a complete zstd stream. Unlike ZSTD_decompress(), does not
// require the frame header to record the content size (which streaming
// compression omits).
inline std::vector<std::byte> ZstdDecompress(std::span<std::byte const> src) {
    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> dctx(
        ZSTD_createDCtx(), &ZSTD_freeDCtx);
    if (!dctx)
        throw std::runtime_error("ZSTD_createDCtx failed");
    std::vector<std::byte> result;
    std::vector<std::byte> buf(ZSTD_DStreamOutSize());
    ZSTD_inBuffer in{src.data(), src.size(), 0};
    std::size_t ret = 0;
    do {
        ZSTD_outBuffer out{buf.data(), buf.size(), 0};
        ret = ZSTD_decompressStream(dctx.get(), &out, &in);
        if (ZSTD_isError(ret))
            throw std::runtime_error(ZSTD_getErrorName(ret));
        result.insert(result.end(), buf.begin(),
                      buf.begin() + static_cast<std::ptrdiff_t>(out.pos));
        if (ret != 0 && in.pos == in.size && out.pos < out.size)
            throw std::runtime_error("truncated zstd stream");
    } while (ret != 0 || in.pos < in.size);
    return result;
}

} // namespace test_support
