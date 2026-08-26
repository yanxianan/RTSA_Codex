#include "sources/SimulatedSpectrumSource.h"

#include "core/FrequencyMapper.h"

#include <QMetaObject>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace rtsa {
namespace {

constexpr double kNanosecondsPerSecond = 1.0e9;

std::uint64_t monotonicNanoseconds()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

float addDecibels(const float firstDb, const float secondDb)
{
    const float largest = std::max(firstDb, secondDb);
    const float smallest = std::min(firstDb, secondDb);
    if (largest - smallest > 60.0F) {
        return largest;
    }
    return largest + 10.0F * std::log10(1.0F + std::pow(10.0F, (smallest - largest) / 10.0F));
}

} // namespace

SimulatedSpectrumSource::SimulatedSpectrumSource(QObject* parent)
    : ISpectrumSource(parent)
{
    statisticsStart_ = std::chrono::steady_clock::now();
    lastStatisticsSample_ = statisticsStart_;
    statisticsStopNs_.store(monotonicNanoseconds(), std::memory_order_relaxed);
}

SimulatedSpectrumSource::~SimulatedSpectrumSource()
{
    stop();
}

void SimulatedSpectrumSource::setFrameSink(FrameSink sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(sink);
}

void SimulatedSpectrumSource::configure(const SimulationConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    config_.binCount = std::clamp<std::size_t>(config_.binCount, 1024U, 65536U);
    config_.frameRate = std::clamp(config_.frameRate, 1.0, 1000.0);
    config_.spanHz = std::max(1.0, config_.spanHz);
    config_.noiseDeviationDb = std::max(0.0F, config_.noiseDeviationDb);
    config_.sweepPeriodSeconds = std::clamp(config_.sweepPeriodSeconds, 0.001, 3600.0);
    config_.transientProbability = std::clamp(config_.transientProbability, 0.0F, 1.0F);
    config_.transientDurationSeconds = std::clamp(
        config_.transientDurationSeconds, 0.001, 60.0);
    config_.faults.pauseEveryFrames = std::min<std::uint32_t>(
        config_.faults.pauseEveryFrames, 1000000U);
    config_.faults.pauseDurationSeconds = std::clamp(
        config_.faults.pauseDurationSeconds, 0.001, 60.0);
    config_.faults.sequenceJumpEveryFrames = std::min<std::uint32_t>(
        config_.faults.sequenceJumpEveryFrames, 1000000U);
    config_.faults.sequenceSkipCount = std::clamp<std::uint32_t>(
        config_.faults.sequenceSkipCount, 1U, 1000000U);
    config_.faults.invalidFrameEveryFrames = std::min<std::uint32_t>(
        config_.faults.invalidFrameEveryFrames, 1000000U);
    config_.faults.burstEveryFrames = std::min<std::uint32_t>(
        config_.faults.burstEveryFrames, 1000000U);
    config_.faults.burstFrameCount = std::clamp<std::uint32_t>(
        config_.faults.burstFrameCount, 1U, 10000U);
    if (config_.tones.size() > 16U) {
        config_.tones.resize(16U);
    }
    ++configurationEpoch_;
    wakeCondition_.notify_all();
}

SimulationConfig SimulatedSpectrumSource::configuration() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool SimulatedSpectrumSource::start(const AcquisitionMode mode)
{
    if (thread_.joinable()) {
        if (state() != SourceState::Stopped && state() != SourceState::Error) {
            return false;
        }
        thread_.join();
    }

    acquisitionMode_ = mode;
    stopRequested_.store(false, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    producedFrames_.store(0, std::memory_order_relaxed);
    producedBytes_.store(0, std::memory_order_relaxed);
    droppedFrames_.store(0, std::memory_order_relaxed);
    statisticsStart_ = std::chrono::steady_clock::now();
    statisticsStopNs_.store(0, std::memory_order_relaxed);
    setState(SourceState::Starting);

    try {
        thread_ = std::thread(&SimulatedSpectrumSource::runGuarded, this);
    } catch (const std::exception& error) {
        setState(SourceState::Error);
        emit errorOccurred(QString::fromUtf8(error.what()));
        return false;
    }
    return true;
}

void SimulatedSpectrumSource::runGuarded() noexcept
{
    try {
        run();
    } catch (const std::exception& error) {
        stopRequested_.store(true, std::memory_order_release);
        setState(SourceState::Error);
        emit errorOccurred(QString::fromUtf8(error.what()));
    } catch (...) {
        stopRequested_.store(true, std::memory_order_release);
        setState(SourceState::Error);
        emit errorOccurred(QStringLiteral("Unexpected simulated source failure."));
    }
}

void SimulatedSpectrumSource::pause()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SourceState expected = SourceState::Running;
        if (!state_.compare_exchange_strong(expected, SourceState::Paused,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            return;
        }
        paused_.store(true, std::memory_order_release);
    }
    wakeCondition_.notify_all();
    publishStateChange(SourceState::Paused);
}

void SimulatedSpectrumSource::resume()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SourceState expected = SourceState::Paused;
        if (!state_.compare_exchange_strong(expected, SourceState::Running,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            return;
        }
        paused_.store(false, std::memory_order_release);
    }
    wakeCondition_.notify_all();
    publishStateChange(SourceState::Running);
}

void SimulatedSpectrumSource::stop()
{
    {
        // The predicate state and notification share mutex_ with every
        // condition-variable wait.  This prevents a stop/resume notification
        // from being lost between the worker's predicate check and sleep.
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_.store(true, std::memory_order_release);
        paused_.store(false, std::memory_order_release);
    }
    wakeCondition_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    setState(SourceState::Stopped);
}

SourceState SimulatedSpectrumSource::state() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

SourceStatistics SimulatedSpectrumSource::statistics() const
{
    const auto now = std::chrono::steady_clock::now();
    const auto frames = producedFrames_.load(std::memory_order_relaxed);
    const auto bytes = producedBytes_.load(std::memory_order_relaxed);
    const std::uint64_t stopNs = statisticsStopNs_.load(std::memory_order_relaxed);
    const auto stopTime = stopNs == 0
        ? now
        : std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::nanoseconds(stopNs)));
    const double uptime = std::max(0.0,
        std::chrono::duration<double>(stopTime - statisticsStart_).count());
    const double elapsed = std::max(1.0e-9, uptime);

    double intervalFrameRate = 0.0;
    double intervalBytesPerSecond = 0.0;
    {
        std::lock_guard<std::mutex> lock(statisticsMutex_);
        const double intervalSeconds = std::max(1.0e-9,
            std::chrono::duration<double>(now - lastStatisticsSample_).count());
        if (state() == SourceState::Running) {
            intervalFrameRate = static_cast<double>(frames - lastStatisticsFrames_)
                / intervalSeconds;
            intervalBytesPerSecond = static_cast<double>(bytes - lastStatisticsBytes_)
                / intervalSeconds;
        }
        lastStatisticsFrames_ = frames;
        lastStatisticsBytes_ = bytes;
        lastStatisticsSample_ = now;
    }
    return SourceStatistics {
        frames,
        bytes,
        droppedFrames_.load(std::memory_order_relaxed),
        static_cast<double>(frames) / elapsed,
        static_cast<double>(bytes) / elapsed,
        intervalFrameRate,
        intervalBytesPerSecond,
        uptime
    };
}

void SimulatedSpectrumSource::run()
{
    setState(SourceState::Running);
    const auto runStart = std::chrono::steady_clock::now();
    auto nextFrameTime = runStart;
    std::uint64_t sequence = 0;
    std::uint64_t frameOrdinal = 0;
    std::uint32_t burstFramesRemaining = 0;
    std::uint32_t randomEpoch = 0;
    activeTransientUntilSeconds_ = -1.0;
    activeTransientFrequencyHz_ = 0.0;

    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (paused_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(mutex_);
            wakeCondition_.wait(lock, [this] {
                return stopRequested_.load(std::memory_order_acquire)
                    || !paused_.load(std::memory_order_acquire);
            });
            nextFrameTime = std::chrono::steady_clock::now();
            continue;
        }

        SimulationConfig config;
        FrameSink sink;
        std::uint32_t epoch = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = config_;
            sink = sink_;
            epoch = configurationEpoch_;
        }
        if (randomEpoch != epoch) {
            // The generator belongs exclusively to this worker thread. Reseeding here
            // avoids a configure/generate data race while keeping runs reproducible.
            randomState_ = config.randomSeed != 0U ? config.randomSeed : 0x47D2023U;
            randomEpoch = epoch;
            activeTransientUntilSeconds_ = -1.0;
            activeTransientFrequencyHz_ = 0.0;
            frameOrdinal = 0;
            burstFramesRemaining = 0;
        }

        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - runStart).count();
        ++frameOrdinal;
        const bool burstFrame = burstFramesRemaining > 0U;
        auto frame = generateFrame(config, epoch, elapsed);
        if (frame) {
            ++sequence;
            if (config.faults.sequenceJumpEveryFrames > 0U
                && frameOrdinal % config.faults.sequenceJumpEveryFrames == 0U) {
                sequence += config.faults.sequenceSkipCount;
                frame->metadata.flags |= SpectrumFrameSequenceJump;
            }
            frame->metadata.sequence = sequence;
            if (burstFrame) {
                frame->metadata.flags |= SpectrumFrameBurst;
            }
            if (config.faults.invalidFrameEveryFrames > 0U
                && frameOrdinal % config.faults.invalidFrameEveryFrames == 0U
                && !frame->bins.empty()) {
                frame->bins.front() = std::numeric_limits<float>::quiet_NaN();
                frame->metadata.flags |= SpectrumFrameInjectedInvalid;
            }
            if (sink) {
                static_cast<void>(sink(frame));
            } else {
                droppedFrames_.fetch_add(1, std::memory_order_relaxed);
            }
            producedFrames_.fetch_add(1, std::memory_order_relaxed);
            producedBytes_.fetch_add(frame->bins.size() * sizeof(float), std::memory_order_relaxed);
        } else {
            droppedFrames_.fetch_add(1, std::memory_order_relaxed);
        }

        if (acquisitionMode_ == AcquisitionMode::SingleFrame) {
            stopRequested_.store(true, std::memory_order_release);
            break;
        }

        if (burstFrame) {
            --burstFramesRemaining;
        }
        if (config.faults.burstEveryFrames > 0U
            && frameOrdinal % config.faults.burstEveryFrames == 0U) {
            burstFramesRemaining = std::max(
                burstFramesRemaining, config.faults.burstFrameCount);
        }

        if (config.faults.pauseEveryFrames > 0U
            && frameOrdinal % config.faults.pauseEveryFrames == 0U) {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto pauseDuration = std::chrono::duration<double>(
                config.faults.pauseDurationSeconds);
            wakeCondition_.wait_for(lock, pauseDuration, [this, epoch] {
                return stopRequested_.load(std::memory_order_acquire)
                    || paused_.load(std::memory_order_acquire)
                    || configurationEpoch_ != epoch;
            });
            nextFrameTime = std::chrono::steady_clock::now();
            continue;
        }

        if (burstFramesRemaining > 0U) {
            continue;
        }

        if (config.unthrottled) {
            nextFrameTime = std::chrono::steady_clock::now();
            continue;
        }

        const auto framePeriod = std::chrono::duration<double>(1.0 / config.frameRate);
        nextFrameTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(framePeriod);
        const auto current = std::chrono::steady_clock::now();
        if (nextFrameTime > current) {
            const auto remaining = nextFrameTime - current;
            if (remaining <= std::chrono::milliseconds(2)) {
                // Windows sleep primitives commonly overshoot a 1 ms period. The
                // simulator uses a bounded spin only for this extreme test rate;
                // normal frame rates and the future blocking DMA source do not.
                while (std::chrono::steady_clock::now() < nextFrameTime
                       && !stopRequested_.load(std::memory_order_relaxed)
                       && !paused_.load(std::memory_order_relaxed)) {
                }
            } else if (remaining <= std::chrono::milliseconds(10)) {
                // condition_variable timers are coarse on some Windows runtimes and
                // can noticeably reduce ordinary high-rate simulation accuracy.
                std::this_thread::sleep_until(nextFrameTime);
            } else {
                // At low frame rates an interruptible wait keeps Stop responsive.
                std::unique_lock<std::mutex> lock(mutex_);
                wakeCondition_.wait_until(lock, nextFrameTime, [this, epoch] {
                    return stopRequested_.load(std::memory_order_acquire)
                        || paused_.load(std::memory_order_acquire)
                        || configurationEpoch_ != epoch;
                });
            }
        } else if (current - nextFrameTime > framePeriod * 10.0) {
            nextFrameTime = current;
        }
    }
    setState(SourceState::Stopped);
}

SpectrumFramePtr SimulatedSpectrumSource::generateFrame(const SimulationConfig& config,
                                                        const std::uint32_t epoch,
                                                        const double elapsedSeconds)
{
    auto frame = framePool_.acquire(config.binCount);
    if (!frame) {
        return {};
    }

    frame->metadata.timestampNs = monotonicNanoseconds();
    frame->metadata.centerFrequencyHz = config.centerFrequencyHz;
    frame->metadata.spanHz = config.spanHz;
    frame->metadata.binCount = static_cast<std::uint32_t>(config.binCount);
    frame->metadata.unit = AmplitudeUnit::Dbfs;
    frame->metadata.calibrated = false;
    frame->metadata.configurationEpoch = epoch;

    for (float& value : frame->bins) {
        value = config.noiseFloorDbfs + nextUnitNoise() * config.noiseDeviationDb;
    }

    for (const auto& tone : config.tones) {
        if (tone.enabled) {
            addTone(frame->bins,
                    config,
                    tone.frequencyHz,
                    tone.amplitudeDbfs,
                    tone.widthHz);
        }
    }

    if (config.sweepEnabled && config.sweepPeriodSeconds > 0.0) {
        const double phase = std::fmod(elapsedSeconds, config.sweepPeriodSeconds)
            / config.sweepPeriodSeconds;
        double position = phase;
        switch (config.sweepDirection) {
        case SweepDirection::Down:
            position = 1.0 - phase;
            break;
        case SweepDirection::PingPong:
            position = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
            break;
        case SweepDirection::Up:
        default:
            break;
        }
        const double sweepFrequency = config.sweepStartHz
            + (config.sweepStopHz - config.sweepStartHz) * position;
        addTone(frame->bins, config, sweepFrequency, config.sweepAmplitudeDbfs, 1.5e6);
    }

    bool transientActive = elapsedSeconds < activeTransientUntilSeconds_;
    if (!transientActive && nextUniform() < config.transientProbability) {
        const double start = config.centerFrequencyHz - config.spanHz * 0.5;
        activeTransientFrequencyHz_ = start + nextUniform() * config.spanHz;
        activeTransientUntilSeconds_ = elapsedSeconds + config.transientDurationSeconds;
        transientActive = true;
    }
    if (transientActive) {
        addTone(frame->bins,
                config,
                activeTransientFrequencyHz_,
                config.transientAmplitudeDbfs,
                1.0e6);
        frame->metadata.flags |= SpectrumFrameTransient;
    }

    return frame;
}

std::uint32_t SimulatedSpectrumSource::nextRandom() noexcept
{
    // xorshift32: small state, deterministic and much cheaper than constructing a
    // standard-library normal distribution for every frequency bin.
    std::uint32_t value = randomState_;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    randomState_ = value;
    return value;
}

float SimulatedSpectrumSource::nextUniform() noexcept
{
    constexpr float inverse24BitRange = 1.0F / 16777216.0F;
    return static_cast<float>(nextRandom() >> 8U) * inverse24BitRange;
}

float SimulatedSpectrumSource::nextUnitNoise() noexcept
{
    // Two independent uniforms form a zero-mean triangular distribution. sqrt(6)
    // normalizes it to unit variance, which is sufficient for realistic display
    // noise while making 16k/65k stress streams substantially faster.
    constexpr float squareRootOfSix = 2.449489743F;
    return (nextUniform() + nextUniform() - 1.0F) * squareRootOfSix;
}

void SimulatedSpectrumSource::addTone(std::vector<float>& bins,
                                      const SimulationConfig& config,
                                      const double frequencyHz,
                                      const float amplitudeDbfs,
                                      const double widthHz)
{
    if (bins.empty() || config.spanHz <= 0.0) {
        return;
    }

    const double startHz = config.centerFrequencyHz - config.spanHz * 0.5;
    const double stopHz = config.centerFrequencyHz + config.spanHz * 0.5;
    if (!std::isfinite(frequencyHz) || frequencyHz < startHz || frequencyHz >= stopHz) {
        return;
    }

    SpectrumMetadata metadata;
    metadata.centerFrequencyHz = config.centerFrequencyHz;
    metadata.spanHz = config.spanHz;
    metadata.binCount = static_cast<std::uint32_t>(bins.size());
    const std::size_t centerBin = FrequencyMapper::nearestBinForFrequency(metadata, frequencyHz);
    const double binResolutionHz = config.spanHz / static_cast<double>(bins.size());
    const float sigmaBins = (binResolutionHz > 0.0 && widthHz > 0.0)
        ? static_cast<float>(widthHz / binResolutionHz)
        : 1.0F;
    const float sigma = std::max(0.5F, sigmaBins);
    const std::size_t radius = static_cast<std::size_t>(std::ceil(sigma * 6.0F));
    const std::size_t begin = centerBin > radius ? centerBin - radius : 0;
    const std::size_t end = std::min(bins.size(), centerBin + radius + 1U);

    for (std::size_t index = begin; index < end; ++index) {
        const float offset = static_cast<float>(static_cast<long long>(index)
            - static_cast<long long>(centerBin));
        const float toneDb = amplitudeDbfs - 2.1714724F * (offset * offset) / (sigma * sigma);
        bins[index] = addDecibels(bins[index], toneDb);
    }
}

void SimulatedSpectrumSource::setState(const SourceState newState)
{
    const SourceState previous = state_.exchange(newState, std::memory_order_acq_rel);
    if (previous != newState) {
        publishStateChange(newState);
    }
}

void SimulatedSpectrumSource::publishStateChange(const SourceState newState)
{
    if (newState == SourceState::Stopped || newState == SourceState::Error) {
        std::uint64_t expected = 0;
        static_cast<void>(statisticsStopNs_.compare_exchange_strong(
            expected, monotonicNanoseconds(), std::memory_order_relaxed));
    }
    {
        std::lock_guard<std::mutex> lock(statisticsMutex_);
        lastStatisticsFrames_ = producedFrames_.load(std::memory_order_relaxed);
        lastStatisticsBytes_ = producedBytes_.load(std::memory_order_relaxed);
        lastStatisticsSample_ = std::chrono::steady_clock::now();
    }
    emit stateChanged(static_cast<int>(newState));
}

} // namespace rtsa
