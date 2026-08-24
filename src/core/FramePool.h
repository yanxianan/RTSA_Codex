#pragma once

#include "core/SpectrumFrame.h"

#include <cstddef>
#include <memory>

namespace rtsa {

class FramePool final {
public:
    explicit FramePool(std::size_t maximumFrames = 8);
    ~FramePool();

    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;

    SpectrumFramePtr acquire(std::size_t binCount);
    std::size_t allocatedCount() const;
    std::size_t availableCount() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace rtsa

