#include "core/LatestFrameStore.h"

#include <atomic>

namespace rtsa {

void LatestFrameStore::publish(ConstSpectrumFramePtr frame) noexcept
{
    std::atomic_store_explicit(&latest_, std::move(frame), std::memory_order_release);
}

ConstSpectrumFramePtr LatestFrameStore::latest() const noexcept
{
    return std::atomic_load_explicit(&latest_, std::memory_order_acquire);
}

void LatestFrameStore::clear() noexcept
{
    std::atomic_store_explicit(&latest_, ConstSpectrumFramePtr {}, std::memory_order_release);
}

} // namespace rtsa

