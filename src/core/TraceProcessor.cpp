#include "core/TraceProcessor.h"

#include <algorithm>
#include <cmath>

namespace rtsa {

void TraceProcessor::setMode(const TraceMode mode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (mode_ != mode) {
        mode_ = mode;
        resetLocked();
    }
}

TraceMode TraceProcessor::mode() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return mode_;
}

void TraceProcessor::setAverageCount(const std::size_t count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto clamped = std::max<std::size_t>(1U, count);
    if (averageCount_ != clamped) {
        averageCount_ = clamped;
        resetLocked();
    }
}

std::size_t TraceProcessor::averageCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return averageCount_;
}

void TraceProcessor::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    resetLocked();
}

std::size_t TraceProcessor::reusableOutputFrameCount() const
{
    return outputPool_.allocatedCount();
}

SpectrumFramePtr TraceProcessor::process(const SpectrumFramePtr& input)
{
    if (!input || !input->isConsistent() || !input->hasFiniteBins()) {
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (mode_ == TraceMode::ClearWrite) {
        return input;
    }

    const auto& metadata = input->metadata;
    const bool configurationChanged = history_.size() != input->bins.size()
        || epoch_ != metadata.configurationEpoch
        || previousMetadata_.centerFrequencyHz != metadata.centerFrequencyHz
        || previousMetadata_.spanHz != metadata.spanHz
        || previousMetadata_.unit != metadata.unit;

    if (configurationChanged) {
        resetLocked();
        epoch_ = metadata.configurationEpoch;
        previousMetadata_ = metadata;
        history_ = input->bins;
        accumulatedFrames_ = 1;
    } else if (mode_ == TraceMode::MaxHold) {
        for (std::size_t i = 0; i < history_.size(); ++i) {
            history_[i] = std::max(history_[i], input->bins[i]);
        }
        ++accumulatedFrames_;
    } else if (mode_ == TraceMode::MinHold) {
        for (std::size_t i = 0; i < history_.size(); ++i) {
            history_[i] = std::min(history_[i], input->bins[i]);
        }
        ++accumulatedFrames_;
    } else {
        const std::size_t nextCount = std::min(averageCount_, accumulatedFrames_ + 1U);
        const float alpha = 1.0F / static_cast<float>(nextCount);
        for (std::size_t i = 0; i < history_.size(); ++i) {
            history_[i] += (input->bins[i] - history_[i]) * alpha;
        }
        accumulatedFrames_ = nextCount;
    }

    auto output = outputPool_.acquire(input->bins.size());
    if (!output) {
        return {};
    }
    output->metadata = input->metadata;
    output->bins = history_;
    return output;
}

void TraceProcessor::resetLocked()
{
    history_.clear();
    accumulatedFrames_ = 0;
    epoch_ = 0;
    previousMetadata_ = SpectrumMetadata {};
}

} // namespace rtsa
