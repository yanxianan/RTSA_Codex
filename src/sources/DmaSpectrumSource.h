#pragma once

#include "core/FramePool.h"
#include "sources/ISpectrumSource.h"
#include "sources/SourceTypes.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace rtsa {

struct DmaSourceConfig {
    QString devicePath = QStringLiteral("/dev/rtsa_dma");
    std::size_t binCount = 1024;
    double centerFrequencyHz = 1.0e9;
    double spanHz = 200.0e6;
    double uiRefreshRateHz = 60.0;             // 界面显示节流目标刷新率（默认 60 fps）
    bool enableFftShift = false;               // 是否进行 FFT Shift（DC 置中）
    float fullScaleMagnitude = 2147483648.0f;  // 2^31 满量程归一化基准
};

class DmaSpectrumSource final : public ISpectrumSource {
    Q_OBJECT

public:
    explicit DmaSpectrumSource(QObject* parent = nullptr);
    ~DmaSpectrumSource() override;

    DmaSpectrumSource(const DmaSpectrumSource&) = delete;
    DmaSpectrumSource& operator=(const DmaSpectrumSource&) = delete;

    void setFrameSink(FrameSink sink) override;
    bool start(AcquisitionMode mode = AcquisitionMode::Continuous) override;
    void pause() override;
    void resume() override;
    void stop() override;

    SourceState state() const noexcept override;
    SourceStatistics statistics() const override;

    void setConfiguration(const DmaSourceConfig& config);
    DmaSourceConfig configuration() const;

private:
    void runGuarded() noexcept;
    void run();
    void setState(SourceState state);

    mutable std::mutex mutex_;
    DmaSourceConfig config_;
    FrameSink sink_;
    std::thread thread_;
    FramePool framePool_ { 64 };

    std::atomic<SourceState> state_ { SourceState::Initialized };
    std::atomic<bool> stopRequested_ { false };
    std::atomic<bool> paused_ { false };
    AcquisitionMode acquisitionMode_ = AcquisitionMode::Continuous;

    std::atomic<std::uint64_t> producedFrames_ { 0 };
    std::atomic<std::uint64_t> producedBytes_ { 0 };
    std::atomic<std::uint64_t> droppedFrames_ { 0 };

    std::chrono::steady_clock::time_point statisticsStart_;
    std::atomic<std::uint64_t> statisticsStopNs_ { 0 };
    mutable std::mutex statisticsMutex_;
    mutable std::uint64_t lastStatisticsFrames_ = 0;
    mutable std::uint64_t lastStatisticsBytes_ = 0;
    mutable std::chrono::steady_clock::time_point lastStatisticsSample_;
};

} // namespace rtsa
