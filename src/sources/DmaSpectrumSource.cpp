#include "sources/DmaSpectrumSource.h"
#include "rtsa_dma_uapi.h"

#include <cmath>
#include <chrono>
#include <algorithm>
#include <limits>
#include <cstring>
#include <QDebug>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <errno.h>
#endif

namespace rtsa {

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
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
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
    setState(SourceState::Paused);
}

void DmaSpectrumSource::resume()
{
    paused_.store(false, std::memory_order_release);
    setState(SourceState::Running);
}

void DmaSpectrumSource::stop()
{
    stopRequested_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
    setState(SourceState::Stopped);
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

    const size_t totalMapSize = static_cast<size_t>(RTSA_RING_BUFFER_COUNT) * RTSA_BUFFER_SIZE;
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

    setState(SourceState::Running);

    const double refreshIntervalSec = (config.uiRefreshRateHz > 0.0)
        ? (1.0 / config.uiRefreshRateHz)
        : 0.0166667; // 默认约 60 fps
    auto lastPublishTime = std::chrono::steady_clock::now();
    auto lastReportTime = lastPublishTime;
    uint64_t intervalFrames = 0;
    bool firstFrameLogged = false;
    std::vector<int32_t> tempRawBuffer(config.binCount, 0);

    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (paused_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        struct pollfd pfd = { fd, POLLIN, 0 };
        const int pollRet = ::poll(&pfd, 1, 100); // 100ms 超时响应 stop
        if (pollRet < 0) {
            if (errno == EINTR) continue;
            qWarning("[DMA] poll() 失败: %s", strerror(errno));
            break;
        }
        if (pollRet == 0) {
            // 超时继续等待
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
            producedBytes_.fetch_add(config.binCount * sizeof(int32_t), std::memory_order_relaxed);
            intervalFrames++;

            if (info.status != 0) {
                droppedFrames_.fetch_add(1, std::memory_order_relaxed);
            }

            const auto now = std::chrono::steady_clock::now();
            const double elapsedSincePublish =
                std::chrono::duration<double>(now - lastPublishTime).count();

            // 检查是否到了 UI 刷新时机（默认 60 fps，即 ~16.6ms）
            if (elapsedSincePublish >= refreshIntervalSec && !hasFrameToPublish) {
                lastPublishTime = now;
                const auto* rawData = reinterpret_cast<const int32_t*>(
                    static_cast<const char*>(mapBase) + (size_t)info.index * RTSA_BUFFER_SIZE);
                if (tempRawBuffer.size() < config.binCount) {
                    tempRawBuffer.resize(config.binCount);
                }
                std::memcpy(tempRawBuffer.data(), rawData,
                            std::min<size_t>(config.binCount * sizeof(int32_t), RTSA_BUFFER_SIZE));
                publishInfo = info;
                hasFrameToPublish = true;
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

        // 硬件槽位已全部释放回驱动，现在安全进行浮点运算与 UI 投递
        if (hasFrameToPublish) {
            DmaSourceConfig currentConfig;
            FrameSink currentSink;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentConfig = config_;
                currentSink = sink_;
            }

            auto frame = framePool_.acquire(currentConfig.binCount);
            if (frame) {
                const float fullScale = currentConfig.fullScaleMagnitude > 0.0f
                    ? currentConfig.fullScaleMagnitude
                    : 2147483648.0f;
                const std::size_t binCount = currentConfig.binCount;
                const bool fftShift = currentConfig.enableFftShift;
                const std::size_t half = binCount / 2;

                int32_t maxRaw = std::numeric_limits<int32_t>::min();
                int32_t minRaw = std::numeric_limits<int32_t>::max();
                std::size_t peakBin = 0;

                for (std::size_t i = 0; i < binCount; ++i) {
                    const std::size_t srcIdx = fftShift ? ((i + half) % binCount) : i;
                    const int32_t rawVal = tempRawBuffer[srcIdx];
                    if (rawVal > maxRaw) {
                        maxRaw = rawVal;
                        peakBin = i;
                    }
                    if (rawVal < minRaw) {
                        minRaw = rawVal;
                    }
                    const float mag = std::abs(static_cast<float>(rawVal));
                    // 转换为 dBFS：20 * log10(|val| / fullScale)
                    frame->bins[i] = (mag > 1.0f)
                        ? (20.0f * std::log10(mag / fullScale))
                        : -140.0f;
                }

                frame->metadata.sequence = publishInfo.sequence;
                frame->metadata.timestampNs = publishInfo.timestamp_ns;
                frame->metadata.centerFrequencyHz = currentConfig.centerFrequencyHz;
                frame->metadata.spanHz = currentConfig.spanHz;
                frame->metadata.binCount = static_cast<std::uint32_t>(binCount);
                frame->metadata.unit = AmplitudeUnit::Dbfs;
                frame->metadata.calibrated = false;

                if (!firstFrameLogged) {
                    firstFrameLogged = true;
                    qInfo("[DMA] 首帧捕获成功！Seq: %llu, 载荷大小: %u bytes, 原始最大值: %d (Bin #%zu), 原始最小值: %d, 峰值电平: %.2f dBFS",
                          static_cast<unsigned long long>(publishInfo.sequence),
                          publishInfo.size,
                          maxRaw, peakBin, minRaw,
                          frame->bins[peakBin]);
                }

                if (currentSink) {
                    currentSink(frame);
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
    ::ioctl(fd, RTSA_IOC_STOP);
    ::munmap(mapBase, totalMapSize);
    ::close(fd);
    setState(SourceState::Stopped);
    qInfo("[DMA] DMA 资源已安全释放。");
#endif
}

} // namespace rtsa
