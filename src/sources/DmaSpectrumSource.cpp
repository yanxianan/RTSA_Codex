#include "sources/DmaSpectrumSource.h"
#include "core/PlRegisterMap.h"
#include "core/SpectrumFrameCodec.h"
#include "rtsa_dma_uapi.h"

#include <cmath>
#include <chrono>
#include <algorithm>
#include <iterator>
#include <limits>
#include <cstring>
#include <QDebug>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <errno.h>
#endif

namespace rtsa {
namespace {

#ifdef __linux__
PlConfig makePlConfig(const DmaSourceConfig& config)
{
    PlConfig plConfig;
    plConfig.centerFrequencyHz =
        static_cast<std::uint64_t>(std::max(0.0, config.centerFrequencyHz));
    plConfig.spanHz = config.hardwareSpanHz;
    plConfig.traceMode = config.traceMode;
    plConfig.accumulationFrames = config.maxHoldFrames == 0U
        ? 1U
        : config.maxHoldFrames;
    plConfig.windowType = config.windowType;
    plConfig.calibrationOffsetQ16_16 = config.calibrationOffsetQ16_16;
    plConfig.configEpoch = config.configEpoch;
    plConfig.dmaMode = config.dmaMode;
    plConfig.cicRate = config.cicRate;
    plConfig.cicTrunc = config.cicTrunc;
    plConfig.hbLevel = config.hbLevel;
    plConfig.maxHoldFrames = config.maxHoldFrames;
    plConfig.ddsFrequencyHz = config.ddsFrequencyHz;
    return plConfig;
}

#endif

} // namespace

DmaSpectrumSource::DmaSpectrumSource(QObject* parent)
    : ISpectrumSource(parent)
{
}

DmaSpectrumSource::~DmaSpectrumSource()
{
    stop();
}

void DmaSpectrumSource::setFrameSink(FrameSink sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(sink);
}

void DmaSpectrumSource::setConfiguration(const DmaSourceConfig& config)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }
    configGeneration_.fetch_add(1, std::memory_order_release);
    wakeWorker();
}

DmaSourceConfig DmaSpectrumSource::configuration() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool DmaSpectrumSource::start([[maybe_unused]] AcquisitionMode mode)
{
#ifndef __linux__
    emit errorOccurred(QStringLiteral("DMA 数据源仅支持在 Linux 平台（板端）运行。"));
    return false;
#else
    if (thread_.joinable()) {
        if (state() != SourceState::Stopped && state() != SourceState::Error) {
            return true;
        }
        thread_.join();
    }

    stopRequested_.store(false, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    acquisitionMode_ = mode;

    producedFrames_.store(0, std::memory_order_relaxed);
    producedBytes_.store(0, std::memory_order_relaxed);
    droppedFrames_.store(0, std::memory_order_relaxed);

    statisticsStart_ = std::chrono::steady_clock::now();
    statisticsStopNs_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(statisticsMutex_);
        lastStatisticsFrames_ = 0;
        lastStatisticsBytes_ = 0;
        lastStatisticsSample_ = statisticsStart_;
    }

    setState(SourceState::Starting);
    try {
        thread_ = std::thread(&DmaSpectrumSource::runGuarded, this);
    } catch (const std::exception& error) {
        setState(SourceState::Error);
        emit errorOccurred(QString::fromUtf8(error.what()));
        return false;
    }
    return true;
#endif
}

void DmaSpectrumSource::pause()
{
    paused_.store(true, std::memory_order_release);
    wakeWorker();
    setState(SourceState::Paused);
}

void DmaSpectrumSource::resume()
{
    paused_.store(false, std::memory_order_release);
    wakeWorker();
    setState(SourceState::Running);
}

void DmaSpectrumSource::stop()
{
    stopRequested_.store(true, std::memory_order_release);
    wakeWorker();
    if (thread_.joinable()) {
        thread_.join();
    }
#ifdef __linux__
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        const int fd = wakeFd_.exchange(-1, std::memory_order_acq_rel);
        if (fd >= 0) {
            ::close(fd);
        }
    }
#endif
    setState(SourceState::Stopped);
}

void DmaSpectrumSource::wakeWorker() noexcept
{
#ifdef __linux__
    std::lock_guard<std::mutex> lock(wakeMutex_);
    const int fd = wakeFd_.load(std::memory_order_acquire);
    if (fd >= 0) {
        const std::uint64_t one = 1U;
        const ssize_t ignored = ::write(fd, &one, sizeof(one));
        (void)ignored;
    }
#endif
}

SourceState DmaSpectrumSource::state() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

SourceStatistics DmaSpectrumSource::statistics() const
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

void DmaSpectrumSource::setState(SourceState state)
{
    state_.store(state, std::memory_order_release);
    emit stateChanged(static_cast<int>(state));
}

void DmaSpectrumSource::runGuarded() noexcept
{
    try {
        run();
    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromUtf8(ex.what()));
        setState(SourceState::Error);
    } catch (...) {
        emit errorOccurred(QStringLiteral("DMA 采集线程发生未知异常。"));
        setState(SourceState::Error);
    }
}

void DmaSpectrumSource::run()
{
#ifdef __linux__
    DmaSourceConfig config;
    FrameSink sink;
    // Read the generation before the config snapshot. A concurrent
    // setConfiguration() then either appears in the snapshot (duplicate apply)
    // or bumps the generation (runtime apply), never getting lost.
    std::uint64_t appliedGeneration =
        configGeneration_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config = config_;
        sink = sink_;
    }

    const QByteArray devPathBytes = config.devicePath.toLocal8Bit();
    qInfo("[DMA] 正在打开设备节点: %s ...", devPathBytes.constData());
    const int fd = ::open(devPathBytes.constData(), O_RDWR);
    if (fd < 0) {
        const QString errStr = QString::fromLocal8Bit(strerror(errno));
        qCritical("[DMA] 打开设备节点失败: %s (errno=%d)", strerror(errno), errno);
        emit errorOccurred(QStringLiteral("无法打开 DMA 设备节点 %1: %2")
                               .arg(config.devicePath, errStr));
        setState(SourceState::Error);
        return;
    }
    qInfo("[DMA] 成功打开设备节点 (fd=%d)", fd);

    __u32 uapiVersion = 0U;
    const int versionResult = ::ioctl(fd, RTSA_IOC_GET_VERSION, &uapiVersion);
    if (versionResult < 0 || uapiVersion != RTSA_UAPI_VERSION) {
        const QString errStr = (versionResult < 0)
            ? QString::fromLocal8Bit(strerror(errno))
            : QStringLiteral("driver=%1, app=%2")
                  .arg(uapiVersion)
                  .arg(RTSA_UAPI_VERSION);
        qCritical("[DMA] UAPI 版本校验失败: driver=%u, app=%u (%s)",
                  uapiVersion, RTSA_UAPI_VERSION, qPrintable(errStr));
        emit errorOccurred(QStringLiteral("DMA UAPI 版本不兼容: %1").arg(errStr));
        ::close(fd);
        setState(SourceState::Error);
        return;
    }

    const size_t totalMapSize = static_cast<size_t>(RTSA_RING_BUFFER_COUNT) * RTSA_SLOT_SIZE;
    void* mapBase = ::mmap(nullptr, totalMapSize, PROT_READ, MAP_SHARED, fd, 0);
    if (mapBase == MAP_FAILED) {
        const QString errStr = QString::fromLocal8Bit(strerror(errno));
        qCritical("[DMA] mmap 映射环形缓冲失败: %s", strerror(errno));
        emit errorOccurred(QStringLiteral("mmap 映射 DMA 环形缓冲失败: %1").arg(errStr));
        ::close(fd);
        setState(SourceState::Error);
        return;
    }
    qInfo("[DMA] 成功映射环形缓冲: base=%p, totalSize=%zu 字节", mapBase, totalMapSize);

    bool epochFilterActive = false;
    std::uint16_t expectedEpoch = config.configEpoch;
    bool staleEpochLogged = false;

    PlRegisterMap plRegisters;
    if (plRegisters.open()) {
        const PlConfig plConfig = makePlConfig(config);
        if (plRegisters.writeConfig(plConfig)) {
            const bool applied =
                plRegisters.waitForActiveEpoch(plConfig.configEpoch, 200);
            epochFilterActive = applied;
            expectedEpoch = plConfig.configEpoch;
            if (!applied) {
                qWarning("[DMA] PL 配置在 200 ms 内未生效: expected_epoch=%u",
                         plConfig.configEpoch);
            }
            qInfo("[DMA] PL 配置已写入 %s: center=%.3f MHz, span=%.3f MHz, "
                  "trace=%u/%u, window=%u, cal=%.1f dB, epoch=%u, "
                  "source=%u, CIC=%u/%u, HB=%u, max_hold=%u, DDS=%.6f MHz",
                  plRegisters.devicePath().c_str(),
                  static_cast<double>(plConfig.centerFrequencyHz) / 1.0e6,
                  static_cast<double>(plConfig.spanHz) / 1.0e6,
                  plConfig.traceMode, plConfig.accumulationFrames,
                  plConfig.windowType,
                  static_cast<double>(plConfig.calibrationOffsetQ16_16) / 65536.0,
                  plConfig.configEpoch,
                  plConfig.dmaMode,
                  plConfig.cicRate,
                  plConfig.cicTrunc,
                  plConfig.hbLevel,
                  plConfig.maxHoldFrames,
                  plConfig.ddsFrequencyHz / 1.0e6);
        } else {
            qWarning("[DMA] 写入 PL 配置失败");
        }
    } else {
        qWarning("[DMA] 未找到 rtsa_regs UIO，使用 PL 默认配置");
    }

    __u32 requestedFrameBytes = config.frameBytes;
    if (requestedFrameBytes == 0U) {
        requestedFrameBytes = static_cast<__u32>(64U + config.binCount * 2U);
    }
    if (::ioctl(fd, RTSA_IOC_SET_FRAME_SIZE, &requestedFrameBytes) < 0) {
        const QString errStr = QString::fromLocal8Bit(strerror(errno));
        qCritical("[DMA] RTSA_IOC_SET_FRAME_SIZE(%u) 失败: %s",
                 requestedFrameBytes, strerror(errno));
        emit errorOccurred(QStringLiteral(
            "无法设置 DMA 帧长 %1: %2").arg(requestedFrameBytes).arg(errStr));
        ::munmap(mapBase, totalMapSize);
        ::close(fd);
        setState(SourceState::Error);
        return;
    }

    if (::ioctl(fd, RTSA_IOC_START) < 0) {
        const QString errStr = QString::fromLocal8Bit(strerror(errno));
        qCritical("[DMA] RTSA_IOC_START 启动失败: %s", strerror(errno));
        emit errorOccurred(QStringLiteral("启动 DMA 接收失败 (RTSA_IOC_START): %1").arg(errStr));
        ::munmap(mapBase, totalMapSize);
        ::close(fd);
        setState(SourceState::Error);
        return;
    }
    qInfo("[DMA] RTSA_IOC_START 启动成功，进入采集循环...");

    const int localWakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (localWakeFd >= 0) {
        wakeFd_.store(localWakeFd, std::memory_order_release);
    } else {
        qWarning("[DMA] eventfd 创建失败: %s；配置响应受 poll 超时限制",
                 strerror(errno));
    }

    setState(SourceState::Running);

    const double refreshIntervalSec = (config.uiRefreshRateHz > 0.0)
        ? (1.0 / config.uiRefreshRateHz)
        : 0.0166667; // 默认约 60 fps
    auto lastPublishTime = std::chrono::steady_clock::now();
    auto lastReportTime = lastPublishTime;
    uint64_t intervalFrames = 0;
    bool firstFrameLogged = false;
    std::vector<std::uint8_t> publishBuffer;
    bool runtimeUioWarningLogged = false;
    std::chrono::steady_clock::time_point configEpochWaitStart;
    bool configEpochWaitActive = false;

    while (!stopRequested_.load(std::memory_order_acquire)) {
        const std::uint64_t requestedGeneration =
            configGeneration_.load(std::memory_order_acquire);
        if (requestedGeneration != appliedGeneration) {
            DmaSourceConfig candidate;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                candidate = config_;
            }

            bool configWritten = false;
            bool configApplied = false;
            if (plRegisters.isOpen()) {
                const PlConfig plConfig = makePlConfig(candidate);
                // Stage + commit only. The PL takes the atomic snapshot and
                // applies it at the next frame boundary. The DMA descriptor
                // ring is intentionally not touched: no STOP, no START, no
                // reallocation, no loss of the streaming context.
                configWritten = plRegisters.writeConfig(plConfig);

                if (configWritten) {
                    configApplied =
                        plRegisters.waitForActiveEpoch(plConfig.configEpoch, 200);
                    expectedEpoch = plConfig.configEpoch;
                    epochFilterActive = configApplied;
                    staleEpochLogged = false;
                    configEpochWaitStart = std::chrono::steady_clock::now();
                    configEpochWaitActive = !configApplied;
                    if (!configApplied) {
                        qWarning("[DMA] 运行中配置在 200 ms 内未生效: "
                                 "expected_epoch=%u", plConfig.configEpoch);
                    }
                }
            } else if (!runtimeUioWarningLogged) {
                runtimeUioWarningLogged = true;
                qWarning("[DMA] 运行中修改参数，但 rtsa_regs UIO 不可用；配置无法即时下发");
            }

            if (configWritten) {
                config = candidate;
                appliedGeneration = requestedGeneration;
                if (configApplied) {
                    qInfo("[DMA] 运行中配置已提交并生效: epoch=%u, "
                          "frame-boundary, source=%u, CIC=%u/%u, HB=%u, "
                          "max_hold=%u, DDS=%.6f MHz",
                          candidate.configEpoch,
                          candidate.dmaMode,
                          candidate.cicRate,
                          candidate.cicTrunc,
                          candidate.hbLevel,
                          candidate.maxHoldFrames,
                          candidate.ddsFrequencyHz / 1.0e6);
                } else {
                    qWarning("[DMA] 运行中配置已写入，但 PL 未确认生效");
                }
            } else {
                // Avoid a retry storm. A later user edit gets a new generation
                // and will try again.
                appliedGeneration = requestedGeneration;
                qWarning("[DMA] 运行中配置下发失败，继续使用上一组配置");
            }
            continue;
        }

        if (paused_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        struct pollfd pfds[2] = {
            { fd, POLLIN, 0 },
            { localWakeFd, POLLIN, 0 }
        };
        const nfds_t pollCount = localWakeFd >= 0 ? 2U : 1U;
        const int pollRet = ::poll(pfds, pollCount, 20);
        if (pollRet < 0) {
            if (errno == EINTR) continue;
            qWarning("[DMA] poll() 失败: %s", strerror(errno));
            break;
        }
        if (pollRet == 0) {
            // 超时继续等待
            continue;
        }
        if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            qWarning("[DMA] 设备节点异常: revents=0x%x", pfds[0].revents);
            break;
        }
        if (localWakeFd >= 0 && (pfds[1].revents & POLLIN)) {
            std::uint64_t wakeCount = 0U;
            const ssize_t ignored = ::read(localWakeFd, &wakeCount, sizeof(wakeCount));
            (void)ignored;
            // Process the new configuration at the top of the loop before
            // draining more frames, so UI edits take effect immediately.
            continue;
        }
        if ((pfds[0].revents & POLLIN) == 0) {
            continue;
        }

        bool hasFrameToPublish = false;
        struct rtsa_buffer_info publishInfo {};

        // 限制单次 poll 后的最大排空批次（例如 64 帧），防止在 30 万 fps 超高吞吐下
        // 陷入无限循环，导致无法退出循环投递 UI 帧，并占满 CPU 饿死 GUI 渲染线程
        constexpr int kMaxBatchPerPoll = 64;
        int batchCount = 0;

        struct rtsa_buffer_info info;
        while (!stopRequested_.load(std::memory_order_relaxed) &&
               batchCount < kMaxBatchPerPoll &&
               ::ioctl(fd, RTSA_IOC_DEQUEUE, &info) == 0) {
            batchCount++;
            producedFrames_.fetch_add(1, std::memory_order_relaxed);
            producedBytes_.fetch_add(info.size, std::memory_order_relaxed);
            intervalFrames++;

            if (info.status != 0) {
                droppedFrames_.fetch_add(1, std::memory_order_relaxed);
            } else {
                const auto now = std::chrono::steady_clock::now();
                const double elapsedSincePublish =
                    std::chrono::duration<double>(now - lastPublishTime).count();

                // 检查是否到了 UI 刷新时机（默认 60 fps，即 ~16.6ms）
                if (elapsedSincePublish >= refreshIntervalSec && !hasFrameToPublish) {
                    lastPublishTime = now;
                    const auto* slotData = static_cast<const std::uint8_t*>(mapBase)
                        + static_cast<std::size_t>(info.index) * RTSA_SLOT_SIZE;
                    const std::size_t copyBytes = std::min<std::size_t>(
                        info.size, RTSA_SLOT_SIZE);
                    publishBuffer.assign(slotData, slotData + copyBytes);
                    publishInfo = info;
                    hasFrameToPublish = true;
                }
            }

            // 无论是否推送到 UI，必须以纳秒级速度立即归还槽位给驱动以维持硬件流转
            if (::ioctl(fd, RTSA_IOC_QUEUE, &info.index) < 0) {
                if (errno != EBUSY) {
                    qWarning("[DMA] 归还 slot %u 失败: %s", info.index, strerror(errno));
                }
            }
        }

        // 如果在极高数据率下持续排空满了批次，主动让出 CPU 时间片，防止 GUI 事件循环饥饿
        if (batchCount >= kMaxBatchPerPoll && !hasFrameToPublish) {
            std::this_thread::yield();
        }

        // 硬件槽位已全部释放回驱动，现在安全解析本地副本并投递 UI
        if (hasFrameToPublish) {
            FrameSink currentSink;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentSink = sink_;
            }

            SpectrumFrameHeader header;
            SpectrumFrameParseError parseError = SpectrumFrameParseError::None;
            if (!parseSpectrumFrameHeader(publishBuffer.data(), publishBuffer.size(),
                                          header, parseError)) {
                droppedFrames_.fetch_add(1, std::memory_order_relaxed);
                qWarning("[DMA] 帧头解析失败: error=%d, size=%zu",
                         static_cast<int>(parseError), publishBuffer.size());
            } else if (publishBuffer.size() < header.frameBytes) {
                droppedFrames_.fetch_add(1, std::memory_order_relaxed);
                qWarning("[DMA] 短帧: frame_bytes=%u, received=%zu",
                         header.frameBytes, publishBuffer.size());
            } else if (epochFilterActive && header.configEpoch != expectedEpoch) {
                const auto now = std::chrono::steady_clock::now();
                if (configEpochWaitActive
                    && now - configEpochWaitStart
                        > std::chrono::seconds(1)) {
                    epochFilterActive = false;
                    configEpochWaitActive = false;
                    qWarning("[DMA] 等待 epoch=%u 新帧超过 1 秒，"
                             "暂时接受当前 epoch=%u；请检查 PL commit 路径",
                             expectedEpoch, header.configEpoch);
                }
                if (!staleEpochLogged) {
                    staleEpochLogged = true;
                    qInfo("[DMA] 等待新配置帧: frame_epoch=%u, expected_epoch=%u",
                          header.configEpoch, expectedEpoch);
                }
            } else {
                configEpochWaitActive = false;
                auto frame = framePool_.acquireLog(header.binCount);
                if (frame) {
                    const std::size_t payloadBytes =
                        publishBuffer.size() - header.headerBytes;
                    if (!copyInt16LogBins(
                            publishBuffer.data() + header.headerBytes,
                            payloadBytes,
                            header.binCount,
                            header.fftShift(),
                            frame->logBins,
                            parseError)) {
                        droppedFrames_.fetch_add(1, std::memory_order_relaxed);
                        qWarning("[DMA] payload 解析失败: error=%d",
                                 static_cast<int>(parseError));
                    } else {
                        const double spanHz = static_cast<double>(header.spanHz);
                        const double rbwHz = spanHz > 0.0
                            ? spanHz / static_cast<double>(header.binCount)
                            : 0.0;
                        frame->metadata.sequence = header.sequence;
                        frame->metadata.timestampNs = header.timestampTicks != 0U
                            ? static_cast<std::uint64_t>(
                                  static_cast<double>(header.timestampTicks) * 3.2)
                            : publishInfo.timestamp_ns;
                        frame->metadata.completionTimestampNs = publishInfo.timestamp_ns;
                        frame->metadata.plTimestampTicks = header.timestampTicks;
                        frame->metadata.centerFrequencyHz =
                            static_cast<double>(header.centerFrequencyHz);
                        frame->metadata.spanHz = spanHz;
                        frame->metadata.rbwHz = rbwHz;
                        frame->metadata.enbwHz =
                            rbwHz * windowEnbwFactor(header.windowType);
                        frame->metadata.calibrationOffsetDb =
                            static_cast<double>(header.calibrationOffsetDbQ16_16)
                            / 65536.0;
                        frame->metadata.binCount = header.binCount;
                        frame->metadata.accumulationFrames =
                            std::max<std::uint16_t>(1U, header.accumulationFrames);
                        frame->metadata.traceMode = header.traceMode;
                        frame->metadata.windowType = header.windowType;
                        frame->metadata.dataFormat = header.dataFormat;
                        frame->metadata.configEpoch = header.configEpoch;
                        frame->metadata.configurationEpoch = header.configEpoch;
                        frame->metadata.fftShift = true;
                        frame->metadata.calibrated = header.calibrated();
                        frame->metadata.unit =
                            header.unit == 1U ? AmplitudeUnit::Dbm
                            : header.unit == 2U ? AmplitudeUnit::Dbc
                            : AmplitudeUnit::Dbfs;
                        frame->metadata.flags = header.flags;

                        if (!firstFrameLogged) {
                            firstFrameLogged = true;
                            const auto peakIt = std::max_element(
                                frame->logBins.begin(), frame->logBins.end());
                            const std::size_t peakBin = peakIt == frame->logBins.end()
                                ? 0U
                                : static_cast<std::size_t>(
                                      std::distance(frame->logBins.begin(), peakIt));
                            qInfo("[DMA] 首帧捕获成功！Seq: %llu, frame_bytes=%u, "
                                  "bin_count=%u, span=%.3f MHz, trace=%u/%u, "
                                  "peak_bin=%zu, peak=%.2f dBFS",
                                  static_cast<unsigned long long>(header.sequence),
                                  header.frameBytes,
                                  header.binCount,
                                  spanHz / 1.0e6,
                                  header.traceMode,
                                  header.accumulationFrames,
                                  peakBin,
                                  frame->binDb(peakBin));
                        }

                        if (currentSink) {
                            currentSink(frame);
                        }
                    }
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const double diffSec = std::chrono::duration<double>(now - lastReportTime).count();
        if (diffSec >= 2.0) {
            const double fps = static_cast<double>(intervalFrames) / diffSec;
            qInfo("[DMA 运行状态] 实时硬件接收率: %.1f fps, 累计丢帧: %llu",
                  fps, static_cast<unsigned long long>(droppedFrames_.load(std::memory_order_relaxed)));
            intervalFrames = 0;
            lastReportTime = now;
        }

        if (acquisitionMode_ == AcquisitionMode::SingleFrame && hasFrameToPublish) {
            stopRequested_.store(true, std::memory_order_release);
            break;
        }
    }

    qInfo("[DMA] 停止采集，正在注销 DMA 资源...");
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        if (localWakeFd >= 0) {
            wakeFd_.store(-1, std::memory_order_release);
            ::close(localWakeFd);
        }
    }
    ::ioctl(fd, RTSA_IOC_STOP);
    ::munmap(mapBase, totalMapSize);
    ::close(fd);
    setState(SourceState::Stopped);
    qInfo("[DMA] DMA 资源已安全释放。");
#endif
}

} // namespace rtsa
