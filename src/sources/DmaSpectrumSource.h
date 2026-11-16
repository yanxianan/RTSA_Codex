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
    std::uint32_t frameBytes = 64U + 1024U * 2U;
    std::uint64_t hardwareSpanHz = 9765625U;
    double centerFrequencyHz = 1.0e9;
    double spanHz = 200.0e6;
    double uiRefreshRateHz = 60.0;             // 界面显示节流目标刷新率（默认 60 fps）
    std::uint8_t traceMode = 1U;               // PL default: Max Hold
    std::uint16_t accumulationFrames = 80U;
    std::uint8_t dmaMode = 1U;                 // 1=DDS, 2=RFADC0, 3=RFADC1
    std::uint16_t cicRate = 8U;                // 1=CIC bypass, 4..8192 normal
    std::uint8_t cicTrunc = 31U;               // 0..80
    std::uint8_t hbLevel = 2U;                 // 0..6
    std::uint16_t maxHoldFrames = 80U;
    double ddsFrequencyHz = 1.5e6;
    std::uint8_t windowType = 4U;              // Blackman-Harris
    std::int32_t calibrationOffsetQ16_16 = -12451840; // -190.0 dB
    std::uint16_t configEpoch = 1U;
    // Kept for source compatibility with the old simulation/DMA configuration.
    // The DMA payload now carries dBFS values and FFT-shift metadata in its header.
    bool enableFftShift = false;
    float fullScaleMagnitude = 2147483648.0f;
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
    void wakeWorker() noexcept;

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
    std::atomic<std::uint64_t> configGeneration_ { 0 };
    std::atomic<int> wakeFd_ { -1 };
    mutable std::mutex wakeMutex_;

    std::chrono::steady_clock::time_point statisticsStart_;
    std::atomic<std::uint64_t> statisticsStopNs_ { 0 };
    mutable std::mutex statisticsMutex_;
    mutable std::uint64_t lastStatisticsFrames_ = 0;
    mutable std::uint64_t lastStatisticsBytes_ = 0;
    mutable std::chrono::steady_clock::time_point lastStatisticsSample_;
};

} // namespace rtsa
