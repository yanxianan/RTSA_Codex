#pragma once

#include "core/SpectrumFrame.h"

#include <memory>

namespace rtsa {

class LatestFrameStore final {
public:
    void publish(ConstSpectrumFramePtr frame) noexcept;
    ConstSpectrumFramePtr latest() const noexcept;
    void clear() noexcept;

private:
    mutable ConstSpectrumFramePtr latest_;
};

} // namespace rtsa

