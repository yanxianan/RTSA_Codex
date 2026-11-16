#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace rtsa {

struct TimingSummary {
    double lastMs = 0.0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
};

class RollingTiming final {
public:
    static constexpr std::size_t kCapacity = 256U;

    void add(const double milliseconds)
    {
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
            return;
        }
        lastMs_ = milliseconds;
        samples_[nextIndex_] = milliseconds;
        nextIndex_ = (nextIndex_ + 1U) % kCapacity;
        count_ = std::min(count_ + 1U, kCapacity);
    }

    TimingSummary summary() const
    {
        TimingSummary result;
        result.lastMs = lastMs_;
        if (count_ == 0U) {
            return result;
        }

        std::array<double, kCapacity> sorted {};
        std::copy_n(samples_.begin(), count_, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count_));

        result.p50Ms = percentile(sorted, count_, 50U);
        result.p95Ms = percentile(sorted, count_, 95U);
        result.p99Ms = percentile(sorted, count_, 99U);
        return result;
    }

private:
    static double percentile(const std::array<double, kCapacity>& sorted,
                             const std::size_t count,
                             const std::size_t percent)
    {
        const std::size_t rank = std::min(
            count - 1U,
            (count * percent + 99U) / 100U - 1U);
        return sorted[rank];
    }

    std::array<double, kCapacity> samples_ {};
    std::size_t count_ = 0U;
    std::size_t nextIndex_ = 0U;
    double lastMs_ = 0.0;
};

} // namespace rtsa
