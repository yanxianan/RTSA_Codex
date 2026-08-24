#include "core/SpectrumPipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace rtsa {

bool SpectrumPipeline::submit(const SpectrumFramePtr& frame)
{
    const auto start = std::chrono::steady_clock::now();
    const auto recordDuration = [this, start] {
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        lastProcessingNanoseconds_.store(
            static_cast<std::uint64_t>(std::max<std::int64_t>(0, nanoseconds)),
            std::memory_order_relaxed);
    };
    submittedFrames_.fetch_add(1, std::memory_order_relaxed);
    if (!frame || !frame->isConsistent() || !frame->hasFiniteBins()
        || frame->metadata.binCount < 2
        || frame->metadata.spanHz <= 0.0
        || !std::isfinite(frame->metadata.centerFrequencyHz)
        || !std::isfinite(frame->metadata.spanHz)) {
        invalidFrames_.fetch_add(1, std::memory_order_relaxed);
        recordDuration();
        return false;
    }

    auto processed = traceProcessor_.process(frame);
    if (!processed) {
        processingDrops_.fetch_add(1, std::memory_order_relaxed);
        recordDuration();
        return false;
    }

    processed->metadata.publicationSequence =
        publishedFrames_.fetch_add(1, std::memory_order_relaxed) + 1U;
    latestFrame_.publish(processed);
    recordDuration();
    return true;
}

void SpectrumPipeline::setTraceMode(const TraceMode mode)
{
    traceProcessor_.setMode(mode);
}

void SpectrumPipeline::setAverageCount(const std::size_t count)
{
    traceProcessor_.setAverageCount(count);
}

void SpectrumPipeline::resetTrace()
{
    traceProcessor_.reset();
}

ConstSpectrumFramePtr SpectrumPipeline::latest() const noexcept
{
    return latestFrame_.latest();
}

PipelineStatistics SpectrumPipeline::statistics() const noexcept
{
    return PipelineStatistics {
        submittedFrames_.load(std::memory_order_relaxed),
        publishedFrames_.load(std::memory_order_relaxed),
        invalidFrames_.load(std::memory_order_relaxed),
        processingDrops_.load(std::memory_order_relaxed),
        static_cast<double>(lastProcessingNanoseconds_.load(
            std::memory_order_relaxed)) / 1.0e6,
        0U
    };
}

void SpectrumPipeline::clear()
{
    latestFrame_.clear();
    traceProcessor_.reset();
}

} // namespace rtsa
