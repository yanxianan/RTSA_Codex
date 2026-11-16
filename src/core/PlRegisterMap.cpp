#include "core/PlRegisterMap.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <thread>

#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#endif

namespace rtsa {
namespace {

constexpr std::size_t kRegisterWindowBytes = 8192U;
constexpr double kPhaseAccumulatorModulus = 4294967296.0;

bool rangeAllowed(const std::uint32_t offset) noexcept
{
    return (offset % 4U) == 0U
        && (static_cast<std::size_t>(offset) + sizeof(std::uint32_t)
            <= kRegisterWindowBytes);
}

} // namespace

std::uint32_t ddsPhaseStepWord(const double frequencyHz) noexcept
{
    if (!std::isfinite(frequencyHz)) {
        return 0U;
    }
    const double clamped = std::clamp(
        frequencyHz, 0.0, kPlDdsEffectiveSampleRateHz * 0.5);
    const double word = clamped * kPhaseAccumulatorModulus
        / kPlDdsEffectiveSampleRateHz;
    return static_cast<std::uint32_t>(std::llround(word));
}

double ddsFrequencyFromPhaseStep(const std::uint32_t phaseStep) noexcept
{
    return static_cast<double>(phaseStep)
        * kPlDdsEffectiveSampleRateHz / kPhaseAccumulatorModulus;
}

std::uint16_t normalizeCicRate(const std::uint16_t cicRate) noexcept
{
    if (cicRate <= 1U) {
        return 1U;
    }
    return std::clamp<std::uint16_t>(cicRate, 4U, 8192U);
}

double plDecimatedSampleRateHz(const std::uint16_t cicRate,
                               const std::uint8_t hbLevel) noexcept
{
    const double rate = static_cast<double>(normalizeCicRate(cicRate));
    const double halfbandDecimation = static_cast<double>(
        1ULL << std::min<std::uint8_t>(hbLevel, 6U));
    return kPlAdcSampleRateHz / (rate * halfbandDecimation);
}

PlRegisterMap::~PlRegisterMap()
{
    close();
}

bool PlRegisterMap::open(const std::string& name)
{
    close();
#ifdef __linux__
    namespace fs = std::filesystem;
    std::string uioPath;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator("/sys/class/uio", error)) {
        const std::string candidate = entry.path().string();
        std::ifstream nameFile(candidate + "/name");
        std::string candidateName;
        if (nameFile && std::getline(nameFile, candidateName)
            && candidateName == name) {
            uioPath = candidate;
            break;
        }
    }
    if (error || uioPath.empty()) {
        return false;
    }

    const std::string device = "/dev/" + fs::path(uioPath).filename().string();
    const int fd = ::open(device.c_str(), O_RDWR | O_SYNC);
    if (fd < 0) {
        return false;
    }

    std::size_t mapBytes = kRegisterWindowBytes;
    std::ifstream sizeFile(uioPath + "/maps/map0/size");
    std::string sizeText;
    if (sizeFile && std::getline(sizeFile, sizeText)) {
        try {
            const auto reported = static_cast<std::size_t>(
                std::stoull(sizeText, nullptr, 0));
            if (reported >= sizeof(std::uint32_t)) {
                mapBytes = std::min(reported, kRegisterWindowBytes);
            }
        } catch (...) {
            // Keep the historical 8 KiB mapping when the attribute is not
            // present or is not parseable.
        }
    }

    void* mapping = ::mmap(nullptr, mapBytes,
                           PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        ::close(fd);
        return false;
    }

    fd_ = fd;
    memory_ = static_cast<volatile std::uint32_t*>(mapping);
    mappedBytes_ = mapBytes;
    words_ = mappedBytes_ / sizeof(std::uint32_t);
    ownsMemory_ = true;
    devicePath_ = device;
    return true;
#else
    (void)name;
    return false;
#endif
}

void PlRegisterMap::close()
{
#ifdef __linux__
    if (ownsMemory_ && memory_ != nullptr && mappedBytes_ != 0U) {
        ::munmap(const_cast<std::uint32_t*>(memory_), mappedBytes_);
    }
    if (fd_ >= 0) {
        ::close(fd_);
    }
#endif
    memory_ = nullptr;
    words_ = 0U;
    mappedBytes_ = 0U;
    fd_ = -1;
    ownsMemory_ = false;
    devicePath_.clear();
}

bool PlRegisterMap::isOpen() const noexcept
{
    return memory_ != nullptr && words_ > 0U;
}

bool PlRegisterMap::attachMemoryForTest(std::uint32_t* memory,
                                        const std::size_t words) noexcept
{
    close();
    if (memory == nullptr || words == 0U) {
        return false;
    }
    memory_ = memory;
    words_ = std::min<std::size_t>(
        words, kRegisterWindowBytes / sizeof(std::uint32_t));
    mappedBytes_ = words_ * sizeof(std::uint32_t);
    ownsMemory_ = false;
    devicePath_.clear();
    return true;
}

bool PlRegisterMap::writeRegister(const std::uint32_t offset,
                                  const std::uint32_t value) noexcept
{
    if (!isOpen() || !rangeAllowed(offset)
        || (offset / sizeof(std::uint32_t)) >= words_) {
        return false;
    }
    memory_[offset / sizeof(std::uint32_t)] = value;
    return true;
}

bool PlRegisterMap::writeFrameFields(const PlConfig& config) noexcept
{
    bool ok = writeRegister(kPlRegCenterLow,
                            static_cast<std::uint32_t>(config.centerFrequencyHz));
    ok = writeRegister(kPlRegCenterHigh,
                       static_cast<std::uint32_t>(config.centerFrequencyHz >> 32U)) && ok;
    ok = writeRegister(kPlRegSpanLow,
                       static_cast<std::uint32_t>(config.spanHz)) && ok;
    ok = writeRegister(kPlRegSpanHigh,
                       static_cast<std::uint32_t>(config.spanHz >> 32U)) && ok;
    ok = writeRegister(kPlRegTraceAccum,
                       (static_cast<std::uint32_t>(config.accumulationFrames) << 8U)
                       | static_cast<std::uint32_t>(config.traceMode)) && ok;
    ok = writeRegister(kPlRegWindow,
                       static_cast<std::uint32_t>(config.windowType)) && ok;
    ok = writeRegister(kPlRegCalibration,
                       static_cast<std::uint32_t>(config.calibrationOffsetQ16_16)) && ok;
    ok = writeRegister(kPlRegConfigEpoch,
                       static_cast<std::uint32_t>(config.configEpoch)) && ok;
    return ok;
}

bool PlRegisterMap::writeFrameConfig(const PlConfig& config) noexcept
{
    if (!isOpen()) {
        return false;
    }

    bool ok = writeFrameFields(config);
    ok = writeRegister(kPlRegMaxHoldFrames,
                       static_cast<std::uint32_t>(config.maxHoldFrames)) && ok;
    std::uint32_t barrier = 0U;
    ok = readRegister(kPlRegConfigEpoch, barrier) && ok;
    if (!ok) {
        return false;
    }
    ok = writeRegister(kPlRegCommit, 0U) && ok;
    ok = writeRegister(kPlRegCommit, 1U) && ok;
    return ok;
}

bool PlRegisterMap::writeConfig(const PlConfig& config) noexcept
{
    if (!isOpen()) {
        return false;
    }

    const std::uint32_t dmaMode =
        static_cast<std::uint32_t>(std::clamp<std::uint8_t>(config.dmaMode, 1U, 3U));
    const std::uint32_t cicRate =
        static_cast<std::uint32_t>(normalizeCicRate(config.cicRate));
    const std::uint32_t cicTrunc =
        static_cast<std::uint32_t>(std::clamp<std::uint8_t>(
            config.cicTrunc, 0U, 80U));
    const std::uint32_t hbLevel =
        static_cast<std::uint32_t>(std::clamp<std::uint8_t>(
            config.hbLevel, 0U, 6U));
    const std::uint32_t phaseStep = ddsPhaseStepWord(config.ddsFrequencyHz);

    // Stage every datapath/header value first. The PL takes an atomic snapshot
    // only when COMMIT is written, then applies it at the next frame boundary.
    // DDS_RESYNC and CIC_VALID are PL-owned and are never written by the PS.
    bool ok = writeFrameFields(config);
    ok = writeRegister(kPlRegDdsPhaseHigh, phaseStep >> 16U) && ok;
    ok = writeRegister(kPlRegDdsPhaseLow, phaseStep & 0xFFFFU) && ok;
    ok = writeRegister(kPlRegCicRate, cicRate) && ok;
    ok = writeRegister(kPlRegDmaMode, dmaMode) && ok;
    ok = writeRegister(kPlRegCicTrunc, cicTrunc) && ok;
    ok = writeRegister(kPlRegHbLevel, hbLevel) && ok;
    ok = writeRegister(kPlRegMaxHoldFrames,
                       static_cast<std::uint32_t>(config.maxHoldFrames)) && ok;

    // Flush posted UIO writes before the commit snapshot. The read also turns
    // a failed staging transaction into a visible error instead of committing
    // a partially written configuration.
    std::uint32_t barrier = 0U;
    ok = readRegister(kPlRegConfigEpoch, barrier) && ok;
    if (!ok) {
        return false;
    }

    // A fresh commit transaction always starts from 0. The PL snapshots the
    // staging bank on the following 0->1 write. This method is safe while DMA
    // is streaming: no ioctl, descriptor or ring-buffer operation is involved.
    ok = writeRegister(kPlRegCommit, 0U) && ok;
    ok = writeRegister(kPlRegCommit, 1U) && ok;
    return ok;
}

PlConfig PlRegisterMap::readConfig() const noexcept
{
    PlConfig config;
    if (!isOpen()) {
        return config;
    }

    const auto read = [this](const std::uint32_t offset) -> std::uint32_t {
        if (!rangeAllowed(offset) || (offset / sizeof(std::uint32_t)) >= words_) {
            return 0U;
        }
        return memory_[offset / sizeof(std::uint32_t)];
    };

    config.centerFrequencyHz =
        static_cast<std::uint64_t>(read(kPlRegCenterLow))
        | (static_cast<std::uint64_t>(read(kPlRegCenterHigh)) << 32U);
    config.spanHz =
        static_cast<std::uint64_t>(read(kPlRegSpanLow))
        | (static_cast<std::uint64_t>(read(kPlRegSpanHigh)) << 32U);
    const std::uint32_t traceAccum = read(kPlRegTraceAccum);
    config.traceMode = static_cast<std::uint8_t>(traceAccum & 0xFFU);
    config.accumulationFrames =
        static_cast<std::uint16_t>((traceAccum >> 8U) & 0xFFFFU);
    config.windowType = static_cast<std::uint8_t>(read(kPlRegWindow) & 0xFFU);
    config.calibrationOffsetQ16_16 =
        static_cast<std::int32_t>(read(kPlRegCalibration));
    config.configEpoch =
        static_cast<std::uint16_t>(read(kPlRegConfigEpoch) & 0xFFFFU);
    config.dmaMode = static_cast<std::uint8_t>(read(kPlRegDmaMode) & 0x0FU);
    config.cicRate =
        static_cast<std::uint16_t>(read(kPlRegCicRate) & 0xFFFFU);
    config.cicTrunc =
        static_cast<std::uint8_t>(read(kPlRegCicTrunc) & 0xFFU);
    config.hbLevel =
        static_cast<std::uint8_t>(read(kPlRegHbLevel) & 0x07U);
    config.maxHoldFrames =
        static_cast<std::uint16_t>(read(kPlRegMaxHoldFrames) & 0xFFFFU);
    const std::uint32_t phaseStep =
        (read(kPlRegDdsPhaseHigh) << 16U)
        | (read(kPlRegDdsPhaseLow) & 0xFFFFU);
    config.ddsFrequencyHz = ddsFrequencyFromPhaseStep(phaseStep);
    return config;
}

bool PlRegisterMap::readRegister(const std::uint32_t offset,
                                 std::uint32_t& value) const noexcept
{
    if (!isOpen() || !rangeAllowed(offset)
        || (offset / sizeof(std::uint32_t)) >= words_) {
        return false;
    }
    value = memory_[offset / sizeof(std::uint32_t)];
    return true;
}

bool PlRegisterMap::readConfigBusy(bool& busy) const noexcept
{
    std::uint32_t value = 0U;
    if (!readRegister(kPlRegConfigStatus, value)) {
        return false;
    }
    busy = (value & 1U) != 0U;
    return true;
}

bool PlRegisterMap::readActiveEpoch(std::uint16_t& epoch) const noexcept
{
    std::uint32_t value = 0U;
    if (!readRegister(kPlRegActiveEpoch, value)) {
        return false;
    }
    epoch = static_cast<std::uint16_t>(value & 0xFFFFU);
    return true;
}

bool PlRegisterMap::waitForActiveEpoch(const std::uint16_t epoch,
                                       const int timeoutMs) const noexcept
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(std::max(0, timeoutMs));
    do {
        std::uint16_t active = 0U;
        if (readActiveEpoch(active) && active == epoch) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (true);
    return false;
}

std::string PlRegisterMap::devicePath() const
{
    return devicePath_;
}

} // namespace rtsa
