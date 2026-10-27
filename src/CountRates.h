#pragma once

#include "Processing.h"

#include <array>
#include <atomic>
#include <cstddef>

enum class RateTap { LeadingEdge, Photon, TimeCorrelatedPhoton };
inline constexpr std::size_t kNumRateTaps = 3;

// Per-detector-slot count rates (counts per second). Written by the
// processing thread, read by settings getters.
class CountRates {
  public:
    void Set(RateTap tap, std::size_t slot, double cps) noexcept {
        rates_[std::size_t(tap)][slot].store(cps, std::memory_order_relaxed);
    }

    [[nodiscard]] double Get(RateTap tap, std::size_t slot) const noexcept {
        return rates_[std::size_t(tap)][slot].load(std::memory_order_relaxed);
    }

    void Zero() noexcept {
        for (auto &tapRates : rates_)
            for (auto &r : tapRates)
                r.store(0.0, std::memory_order_relaxed);
    }

  private:
    std::array<std::array<std::atomic<double>, kMaxDetectors>, kNumRateTaps>
        rates_{};
};
