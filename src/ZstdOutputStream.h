#pragma once

#include <zstd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// A libtcspc output stream (tcspc::output_stream) that compresses everything
// written to it into a single zstd frame, written to the wrapped output
// stream. The frame is completed when this stream is destroyed.
template <typename Inner> class ZstdOutputStream {
    struct CCtxDeleter {
        void operator()(ZSTD_CCtx *ctx) const noexcept { ZSTD_freeCCtx(ctx); }
    };

    Inner inner_;
    std::unique_ptr<ZSTD_CCtx, CCtxDeleter> ctx_;
    std::vector<std::byte> outBuf_;
    bool error_ = false;

    auto writeOut(ZSTD_outBuffer const &out) noexcept -> bool {
        if (out.pos > 0)
            inner_.write(std::span(outBuf_.data(), out.pos));
        if (inner_.is_error())
            error_ = true;
        return !error_;
    }

    auto compress(ZSTD_inBuffer &in, ZSTD_EndDirective mode) noexcept
        -> std::size_t {
        ZSTD_outBuffer out{outBuf_.data(), outBuf_.size(), 0};
        std::size_t const ret =
            ZSTD_compressStream2(ctx_.get(), &out, &in, mode);
        if (ZSTD_isError(ret))
            error_ = true;
        else
            writeOut(out);
        return ret;
    }

    void finish() noexcept {
        ZSTD_inBuffer in{nullptr, 0, 0};
        while (compress(in, ZSTD_e_end) != 0 && !error_) {
        }
    }

  public:
    ZstdOutputStream(Inner inner, int level)
        : inner_(std::move(inner)), ctx_(ZSTD_createCCtx()),
          outBuf_(ZSTD_CStreamOutSize()) {
        if (!ctx_)
            throw std::runtime_error("Failed to create zstd context");
        std::size_t const ret =
            ZSTD_CCtx_setParameter(ctx_.get(), ZSTD_c_compressionLevel, level);
        if (ZSTD_isError(ret))
            throw std::runtime_error(
                std::string("Failed to set zstd compression level: ") +
                ZSTD_getErrorName(ret));
    }

    ZstdOutputStream(ZstdOutputStream &&) = default;
    ZstdOutputStream(ZstdOutputStream const &) = delete;
    ZstdOutputStream &operator=(ZstdOutputStream &&) = delete;
    ZstdOutputStream &operator=(ZstdOutputStream const &) = delete;

    ~ZstdOutputStream() {
        // Errors while finishing the frame have no way to be reported.
        if (ctx_ && !is_error())
            finish();
    }

    auto is_error() noexcept -> bool { return error_ || inner_.is_error(); }

    static auto tell() noexcept -> std::optional<std::uint64_t> {
        return std::nullopt;
    }

    void write(std::span<std::byte const> buffer) noexcept {
        if (is_error())
            return;
        ZSTD_inBuffer in{buffer.data(), buffer.size(), 0};
        while (in.pos < in.size && !error_)
            compress(in, ZSTD_e_continue);
    }
};
