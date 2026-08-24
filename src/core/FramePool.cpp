#include "core/FramePool.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace rtsa {

struct FramePool::State {
    explicit State(const std::size_t maximum)
        : maximumFrames(std::max<std::size_t>(1U, maximum))
    {
    }

    ~State()
    {
        for (auto* frame : available) {
            delete frame;
        }
    }

    void release(SpectrumFrame* frame)
    {
        if (frame == nullptr) {
            return;
        }
        frame->metadata = SpectrumMetadata {};
        std::lock_guard<std::mutex> lock(mutex);
        available.push_back(frame);
    }

    mutable std::mutex mutex;
    std::vector<SpectrumFrame*> available;
    std::size_t allocated = 0;
    std::size_t maximumFrames = 8;
};

FramePool::FramePool(const std::size_t maximumFrames)
    : state_(std::make_shared<State>(maximumFrames))
{
}

FramePool::~FramePool() = default;

SpectrumFramePtr FramePool::acquire(const std::size_t binCount)
{
    SpectrumFrame* frame = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->available.empty()) {
            frame = state_->available.back();
            state_->available.pop_back();
        } else if (state_->allocated < state_->maximumFrames) {
            frame = new SpectrumFrame();
            ++state_->allocated;
        }
    }

    if (frame == nullptr) {
        return {};
    }

    frame->metadata = SpectrumMetadata {};
    frame->metadata.binCount = static_cast<std::uint32_t>(binCount);
    frame->bins.resize(binCount);

    const auto state = state_;
    return SpectrumFramePtr(frame, [state](SpectrumFrame* released) {
        state->release(released);
    });
}

std::size_t FramePool::allocatedCount() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->allocated;
}

std::size_t FramePool::availableCount() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->available.size();
}

} // namespace rtsa

