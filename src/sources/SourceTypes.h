#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtsa {

enum class SourceState : std::uint8_t {
    Initialized,
    Stopped,
    Starting,
    Running,
    Paused,
    Error
};

enum class AcquisitionMode : std::uint8_t {
    Continuous,
    SingleFrame
};

enum class SweepDirection : std::uint8_t {
    Up,
    Down,
    PingPong
};

struct ToneConfig {
    bool enabled = true;
    double frequencyHz = 1.01e9;
    float amplitudeDbfs = -20.0F;
    double widthHz = 1.5e6;
};

struct SimulationFaultConfig {
    std::uint32_t pauseEveryFrames = 0;
    double pauseDurationSeconds = 0.1;
    std::uint32_t sequenceJumpEveryFrames = 0;
    std::uint32_t sequenceSkipCount = 1;
    std::uint32_t invalidFrameEveryFrames = 0;
    std::uint32_t burstEveryFrames = 0;
    std::uint32_t burstFrameCount = 4;
};

struct SimulationConfig {
    std::size_t binCount = 16384;
    double frameRate = 200.0;
    bool unthrottled = false;
    double centerFrequencyHz = 1.0e9;
    double spanHz = 200.0e6;
    float noiseFloorDbfs = -110.0F;
    float noiseDeviationDb = 1.5F;
    std::uint32_t randomSeed = 0x47D2023U;
    std::vector<ToneConfig> tones {
        ToneConfig { true, 980.0e6, -35.0F, 1.5e6 },
        ToneConfig { true, 1.035e9, -18.0F, 2.5e6 }
    };
    bool sweepEnabled = true;
    double sweepStartHz = 940.0e6;
    double sweepStopHz = 1.06e9;
    double sweepPeriodSeconds = 4.0;
    float sweepAmplitudeDbfs = -45.0F;
    SweepDirection sweepDirection = SweepDirection::Up;
    float transientProbability = 0.002F;
    float transientAmplitudeDbfs = -12.0F;
    double transientDurationSeconds = 0.1;
    SimulationFaultConfig faults;
};

struct SourceStatistics {
    std::uint64_t producedFrames = 0;
    std::uint64_t producedBytes = 0;
    std::uint64_t droppedFrames = 0;
    double actualFrameRate = 0.0;
    double bytesPerSecond = 0.0;
    double intervalFrameRate = 0.0;
    double intervalBytesPerSecond = 0.0;
    double uptimeSeconds = 0.0;
};

} // namespace rtsa
