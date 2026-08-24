#pragma once

#include "core/FramePool.h"
#include "core/SpectrumFrame.h"

#include <cstddef>
#include <mutex>
#include <vector>

namespace rtsa {

enum class TraceMode : std::uint8_t {
    ClearWrite,
    Average,
    MaxHold,
    MinHold
};

class TraceProcessor final {
public:
    void setMode(TraceMode mode);
    TraceMode mode() const;
    void setAverageCount(std::size_t count);
    std::size_t averageCount() const;
    void reset();
    std::size_t reusableOutputFrameCount() const;

    SpectrumFramePtr process(const SpectrumFramePtr& input);

private:
    void resetLocked();

    mutable std::mutex mutex_;
    TraceMode mode_ = TraceMode::ClearWrite;
    std::size_t averageCount_ = 16;
    std::size_t accumulatedFrames_ = 0;
    std::uint32_t epoch_ = 0;
    SpectrumMetadata previousMetadata_;
    std::vector<float> history_;
    FramePool outputPool_ { 8 };
};

} // namespace rtsa
