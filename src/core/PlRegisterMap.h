#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rtsa {

inline constexpr std::uint32_t kPlRegCenterLow = 0x100U;
inline constexpr std::uint32_t kPlRegCenterHigh = 0x104U;
inline constexpr std::uint32_t kPlRegSpanLow = 0x108U;
inline constexpr std::uint32_t kPlRegSpanHigh = 0x10CU;
inline constexpr std::uint32_t kPlRegTraceAccum = 0x110U;
inline constexpr std::uint32_t kPlRegWindow = 0x114U;
inline constexpr std::uint32_t kPlRegCalibration = 0x118U;
inline constexpr std::uint32_t kPlRegConfigEpoch = 0x11CU;
inline constexpr std::uint32_t kPlRegCommit = 0x120U;
inline constexpr std::uint32_t kPlRegConfigStatus = 0x130U;
inline constexpr std::uint32_t kPlRegActiveEpoch = 0x134U;

inline constexpr std::uint32_t kPlRegDdsPhaseHigh = 0x200U;
inline constexpr std::uint32_t kPlRegDdsPhaseLow = 0x204U;
// Reserved: DDS resync is generated inside the PL config sequencer.
inline constexpr std::uint32_t kPlRegDdsResync = 0x208U;
inline constexpr std::uint32_t kPlRegCicRate = 0x20CU;
inline constexpr std::uint32_t kPlRegDmaMode = 0x210U;
inline constexpr std::uint32_t kPlRegCicTrunc = 0x214U;
inline constexpr std::uint32_t kPlRegHbLevel = 0x218U;
// Reserved: CIC_VALID is generated inside the PL config sequencer.
inline constexpr std::uint32_t kPlRegCicConfigValid = 0x21CU;
inline constexpr std::uint32_t kPlRegCicConfigBusy = 0x220U;
inline constexpr std::uint32_t kPlRegMaxHoldFrames = 0x224U;

inline constexpr double kPlAdcSampleRateHz = 312.5e6;
inline constexpr double kPlDdsEffectiveSampleRateHz = 1.25e9;

std::uint32_t ddsPhaseStepWord(double frequencyHz) noexcept;
double ddsFrequencyFromPhaseStep(std::uint32_t phaseStep) noexcept;
std::uint16_t normalizeCicRate(std::uint16_t cicRate) noexcept;
// cicRate==1 means CIC bypass; HB still applies 2^hbLevel decimation.
double plDecimatedSampleRateHz(std::uint16_t cicRate,
                               std::uint8_t hbLevel) noexcept;

struct PlConfig {
    std::uint64_t centerFrequencyHz = 0U;
    std::uint64_t spanHz = 9765625U;
    std::uint8_t traceMode = 1U;              // 0=Raw, 1=Max, 2=Avg, 3=Min
    std::uint16_t accumulationFrames = 80U;
    std::uint8_t windowType = 4U;             // Blackman-Harris
    std::int32_t calibrationOffsetQ16_16 = -12451840; // -190.0 dB
    std::uint16_t configEpoch = 1U;
    std::uint8_t dmaMode = 1U;                // 1=DDS, 2=RFADC0, 3=RFADC1
    std::uint16_t cicRate = 8U;               // 1=bypass, 4..8192 normal
    std::uint8_t cicTrunc = 31U;              // 0..80
    std::uint8_t hbLevel = 2U;                // 0..6, decimation = 2^level
    std::uint16_t maxHoldFrames = 80U;
    double ddsFrequencyHz = 1.5e6;
};

class PlRegisterMap final {
public:
    PlRegisterMap() = default;
    ~PlRegisterMap();

    PlRegisterMap(const PlRegisterMap&) = delete;
    PlRegisterMap& operator=(const PlRegisterMap&) = delete;

    bool open(const std::string& name = "rtsa_regs");
    void close();
    bool isOpen() const noexcept;

    // Stage and commit frame-header/Max-Hold fields only. Safe to call while
    // acquisition is running; the PL applies them at the next frame boundary.
    bool writeFrameConfig(const PlConfig& config) noexcept;

    // Stage and commit the full datapath configuration including DDS/CIC/HB.
    // Safe to call while acquisition is running: the PL resets only its own
    // datapath if needed, while the DMA and descriptor ring keep running.
    bool writeConfig(const PlConfig& config) noexcept;
    PlConfig readConfig() const noexcept;
    bool readRegister(std::uint32_t offset, std::uint32_t& value) const noexcept;
    bool readConfigBusy(bool& busy) const noexcept;
    bool readActiveEpoch(std::uint16_t& epoch) const noexcept;
    bool waitForActiveEpoch(std::uint16_t epoch,
                            int timeoutMs = 200) const noexcept;

    // Test hook: attach a plain memory buffer instead of /dev/uioX.
    bool attachMemoryForTest(std::uint32_t* memory, std::size_t words) noexcept;
    std::string devicePath() const;

private:
    bool writeRegister(std::uint32_t offset, std::uint32_t value) noexcept;
    bool writeFrameFields(const PlConfig& config) noexcept;

    volatile std::uint32_t* memory_ = nullptr;
    std::size_t words_ = 0U;
    std::size_t mappedBytes_ = 0U;
    int fd_ = -1;
    bool ownsMemory_ = false;
    std::string devicePath_;
};

} // namespace rtsa
