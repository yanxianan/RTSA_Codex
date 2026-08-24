#pragma once

#include "core/LatestFrameStore.h"
#include "core/TraceProcessor.h"

#include <atomic>
#include <cstdint>

namespace rtsa {

struct PipelineStatistics {
    std::uint64_t submittedFrames = 0;
    std::uint64_t publishedFrames = 0;
    std::uint64_t invalidFrames = 0;
    std::uint64_t processingDrops = 0;
    double lastProcessingMilliseconds = 0.0;
    std::uint32_t queueDepth = 0;
};

class SpectrumPipeline final {
public:
    bool submit(const SpectrumFramePtr& frame);

    void setTraceMode(TraceMode mode);
    void setAverageCount(std::size_t count);
    void resetTrace();

    ConstSpectrumFramePtr latest() const noexcept;
    PipelineStatistics statistics() const noexcept;
    void clear();

private:
    TraceProcessor traceProcessor_;
    LatestFrameStore latestFrame_;
    std::atomic<std::uint64_t> submittedFrames_ { 0 };
    std::atomic<std::uint64_t> publishedFrames_ { 0 };
    std::atomic<std::uint64_t> invalidFrames_ { 0 };
    std::atomic<std::uint64_t> processingDrops_ { 0 };
    std::atomic<std::uint64_t> lastProcessingNanoseconds_ { 0 };
};

} // namespace rtsa
