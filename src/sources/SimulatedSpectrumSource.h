#pragma once

#include "core/FramePool.h"
#include "sources/ISpectrumSource.h"
#include "sources/ISimulationConfigurable.h"
#include "sources/SourceTypes.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace rtsa {

class SimulatedSpectrumSource final : public ISpectrumSource, public ISimulationConfigurable {
    Q_OBJECT

public:
    explicit SimulatedSpectrumSource(QObject* parent = nullptr);
    ~SimulatedSpectrumSource() override;

    SimulatedSpectrumSource(const SimulatedSpectrumSource&) = delete;
    SimulatedSpectrumSource& operator=(const SimulatedSpectrumSource&) = delete;

    void setFrameSink(FrameSink sink) override;
    void configure(const SimulationConfig& config) override;
    SimulationConfig configuration() const override;

    bool start(AcquisitionMode mode = AcquisitionMode::Continuous) override;
    void pause() override;
    void resume() override;
    void stop() override;

    SourceState state() const noexcept override;
    SourceStatistics statistics() const override;

private:
    void runGuarded() noexcept;
    void run();
    SpectrumFramePtr generateFrame(const SimulationConfig& config,
                                   std::uint32_t epoch,
                                   double elapsedSeconds);
    static void addTone(std::vector<float>& bins,
                        const SimulationConfig& config,
                        double frequencyHz,
                        float amplitudeDbfs,
                        float widthBins);
    std::uint32_t nextRandom() noexcept;
    float nextUniform() noexcept;
    float nextUnitNoise() noexcept;
    void setState(SourceState state);
    void publishStateChange(SourceState state);

    mutable std::mutex mutex_;
    std::condition_variable wakeCondition_;
    SimulationConfig config_;
    FrameSink sink_;
    std::thread thread_;
    FramePool framePool_ { 8 };
    std::uint32_t randomState_ = 0x47D2023U;
    std::atomic<SourceState> state_ { SourceState::Initialized };
    std::atomic<bool> stopRequested_ { false };
    std::atomic<bool> paused_ { false };
    AcquisitionMode acquisitionMode_ = AcquisitionMode::Continuous;
    std::atomic<std::uint64_t> producedFrames_ { 0 };
    std::atomic<std::uint64_t> producedBytes_ { 0 };
    std::atomic<std::uint64_t> droppedFrames_ { 0 };
    std::uint32_t configurationEpoch_ = 1;
    double activeTransientUntilSeconds_ = -1.0;
    double activeTransientFrequencyHz_ = 0.0;
    std::chrono::steady_clock::time_point statisticsStart_;
    std::atomic<std::uint64_t> statisticsStopNs_ { 0 };
    mutable std::mutex statisticsMutex_;
    mutable std::uint64_t lastStatisticsFrames_ = 0;
    mutable std::uint64_t lastStatisticsBytes_ = 0;
    mutable std::chrono::steady_clock::time_point lastStatisticsSample_;
};

} // namespace rtsa
