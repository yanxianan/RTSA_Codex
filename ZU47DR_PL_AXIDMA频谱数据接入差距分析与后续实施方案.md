# ZU47DR PL/AXI DMA 频谱数据接入差距分析与后续实施方案

## 1. 文档目的

本文以最初目标“RFSoC ZU47DR 的 PL 完成 DDC、FFT 和频谱预处理，PS 端 Linux 通过 AXI DMA 接收频谱帧并由 Qt 上位机实时显示”为基线，说明：

1. 当前 Windows 模拟版已经完成了什么；
2. 距离 PetaLinux 板端真实 DMA 显示还缺少什么；
3. AXI DMA 频谱数据率如何计算，与当前模拟数据压力有什么关系；
4. PL、Linux 驱动、PS 应用应采用什么接口和实施顺序；
5. 每个阶段如何测试、验收和控制风险。

本文是后续 PL、驱动和 PS 软件联调的实施基线。协议字段、位宽、时钟和设备 API 在三方签署前仍属于建议，不应直接作为未经确认的硬件常量。

## 2. 结论摘要

当前项目已经完成频谱上位机的主体功能，但完成的是“Windows + 模拟频域数据”阶段，不是“ZU47DR + PetaLinux + AXI DMA”最终阶段。

可以直接复用的部分包括：

- Qt 主界面和高对比度控制面板；
- CPU Raster 频谱绘制；
- Center/Span、Start/Stop 和幅度坐标；
- Clear/Write、Average、Max Hold、Min Hold；
- M1～M4、峰值搜索、Delta 和区间测量；
- PNG、CSV、配置保存和日志；
- 输入/显示速率、数据率、丢帧和延迟统计；
- `SpectrumPipeline`、`LatestFrameStore` 和 GUI 最新帧显示策略；
- `ISpectrumSource` 数据源抽象。

尚未完成的核心部分包括：

- PL 输出频谱数据格式和帧协议冻结；
- AXI DMA 工作模式、缓冲、TLAST 和速率确定；
- Linux 内核驱动或可靠的 DMA Proxy 接口；
- `IDmaDevice`、协议解析器、定点转换器和 `DmaSpectrumSource`；
- UI 参数向 PL/DDC/FFT 控制面的下发与生效确认；
- FFT 缩放、窗函数、RBW、功率语义和 dBFS/dBm 标定；
- PetaLinux Qt 5 SDK 交叉编译、rootfs/QPA 配置和板端运行；
- 板端吞吐、缓存一致性、断流恢复和长时间稳定性验证。

若 PL 每个频点输出 4 字节，`16,384 点 × 1,000 帧/s` 的有效载荷为 `65.536 MB/s`。当前 Windows 模拟器已经以同样的频点数、字节数和帧率达到 `65.54 MB/s`，说明现有处理和显示架构能够承受这一量级的频谱数据；但它不能证明目标板 DMA、DDR、驱动、中断、缓存同步和 AArch64 转换同样能够达到该指标。

## 3. 最初目标数据流

最终系统应形成以下数据路径：

```text
RFDC/ADC
   -> PL DDC/抽取
   -> 加窗
   -> FFT
   -> FFT Shift
   -> 幅度/功率/对数与平均处理
   -> AXI4-Stream 帧
   -> AXI DMA S2MM
   -> PS DDR 环形缓冲
   -> Linux DMA 驱动
   -> DmaSpectrumSource
   -> 协议校验与定点转换
   -> SpectrumPipeline
   -> LatestFrameStore
   -> Qt CPU Raster 频谱显示
```

控制方向还需要一条独立路径：

```text
Qt 参数控件
   -> PS 数据源/设备控制接口
   -> ioctl 或 AXI-Lite/UIO 控制面
   -> PL 配置寄存器
   -> 配置提交与生效确认
   -> 新配置 epoch 写入后续频谱帧头
```

数据面和控制面必须通过配置版本号或 epoch 建立一致性，不能把新 Center/Span 配到旧 DMA 载荷上。

## 4. 当前实现基线

### 4.1 已完成的软件链路

当前运行路径为：

```text
SimulatedSpectrumSource
   -> FramePool
   -> SpectrumPipeline
   -> TraceProcessor
   -> LatestFrameStore
   -> SpectrumPlotWidget
```

模拟源直接生成 FFT 后的 `float32 dBFS` 频谱点，不生成 ADC 或 IQ 时域波形，也不在 PS 端执行 FFT。这与最终 PL 向 PS 输出频谱结果的功能边界一致。

### 4.2 已验证能力

当前 Windows Qt 6 Release 已验证：

- 1,024～65,536 点帧结构和绘图；
- 16,384 点、1,000 帧/s，实测约 `65.54 MB/s`；
- 输入和 GUI 显示解耦，GUI 只取最新完整帧；
- 1920×1080 显示高于 30 FPS；
- 峰值保真的每像素列最小/最大包络；
- 无效帧、序号跳变、暂停和突发故障注入；
- Debug/Release 自动测试；
- Windows 完整运行库部署。

### 4.3 已有接入边界

`ISpectrumSource` 已经定义统一生命周期：

- `start(Continuous)`；
- `start(SingleFrame)`；
- `pause()`；
- `resume()`；
- `stop()`；
- `state()`；
- `statistics()`；
- `FrameSink` 发布只读完整帧。

因此 `DmaSpectrumSource` 接入后不需要重写频谱控件、轨迹、Marker、测量和导出模块。

### 4.4 当前 DMA 路径的实际状态

命令行已经接受：

```text
--source dma
```

但工厂目前会明确返回“DMA 数据源尚不可用”。当前代码中没有：

- DMA 设备打开和关闭；
- DMA 缓冲映射；
- 等待完成帧；
- 帧头解析；
- 原始定点值转换；
- DMA 数据源线程；
- DMA reset 和重试。

换言之，目前完成的是接口预留，而不是 DMA 实现。

## 5. 未完成项和差距矩阵

| 层级 | 当前状态 | 尚未完成 | 完成条件 |
|---|---|---|---|
| PL DDC/FFT | 由其他人员开发 | 输出语义、位宽、顺序、帧率未知 | 形成版本化接口说明和黄金帧 |
| AXI4-Stream | 未接入 | 数据宽度、TKEEP、TLAST、帧间隔未知 | 一帧一 TLAST，ILA 验证通过 |
| AXI DMA | 未接入 | Simple/SG、缓冲数、中断和错误恢复未知 | 连续 S2MM 无覆盖、无半帧 |
| 设备树 | 未实现 | DMA、时钟、中断、reserved memory 节点 | 驱动 probe 成功且资源正确 |
| Linux 驱动 | 未实现/未提供 | 用户态 ABI、缓存同步、poll、reset | 非 root 应用可安全收发和停止 |
| 协议解析 | 未实现 | magic、版本、长度、格式和序号校验 | 单元和模糊测试通过 |
| 原始值转换 | 未实现 | 定点比例、功率/dB 语义、字节序 | 黄金数据逐点一致 |
| DMA 数据源 | 仅有接口 | `DmaSpectrumSource` 和统计/状态机 | 可替换模拟源运行完整 GUI |
| PL 控制面 | 未定义 | Center、Span、FFT、DDC 下发和确认 | 参数生效与数据帧 epoch 一致 |
| 幅度标定 | 未定义 | 窗增益、FFT 归一化、dBFS/dBm 偏移 | 与标准信号源误差满足指标 |
| RBW/功率语义 | 未定义 | 每 bin 功率还是 PSD、ENBW/RBW | 信道功率计算有明确物理意义 |
| Qt 5/AArch64 | 未验证 | SDK、sysroot、Qt Widgets/QPA | 目标可执行文件生成并启动 |
| 板端显示 | 未验证 | linuxfb/eglfs/xcb、字体、触摸 | 目标屏幕正常显示和操作 |
| 板端性能 | 未验证 | DMA/DDR/CPU/GUI 实测 | 达到冻结后的吞吐和延迟指标 |
| 长稳恢复 | 未验证 | 断流、溢出、reset、24 h | 无死锁、无持续内存增长 |

## 6. DMA 数据率计算

### 6.1 基本公式

定义：

- `N`：每帧频点数；
- `B`：每频点有效字节数；
- `F`：每秒输出频谱帧数；
- `H`：每帧协议头字节数；
- `A`：对齐和填充字节数。

有效频谱载荷速率：

```text
PayloadRate = N × B × F
```

DMA 实际有效字节率：

```text
DmaRate = (H + N × B + A) × F
```

若协议头为 64 字节，在 16,384 点、4 字节/点时，头部仅占约 `0.098%`，不是主要带宽来源。

Span 不直接决定 DMA 数据率。对于固定 N、B、F，Span 从 10 MHz 改为 1 GHz，帧字节数不变；Span 只改变每个 bin 对应的频率间隔。

### 6.2 当前内部 float32 格式数据率

当前模拟器和内部 `SpectrumFrame` 都使用每频点 4 字节 `float32`：

| 频点数 | 单帧载荷 | 200 帧/s | 1,000 帧/s |
|---:|---:|---:|---:|
| 1,024 | 4 KiB | 0.8192 MB/s | 4.096 MB/s |
| 4,096 | 16 KiB | 3.2768 MB/s | 16.384 MB/s |
| 16,384 | 64 KiB | 13.1072 MB/s | 65.536 MB/s |
| 32,768 | 128 KiB | 26.2144 MB/s | 131.072 MB/s |
| 65,536 | 256 KiB | 52.4288 MB/s | 262.144 MB/s |

表中 MB/s 使用十进制单位。`65.536 MB/s` 等于 `62.5 MiB/s`。

### 6.3 不同 PL 输出格式比较

| 格式 | 字节/点 | 16,384 点 200 FPS | 16,384 点 1,000 FPS | 65,536 点 200 FPS | 65,536 点 1,000 FPS |
|---|---:|---:|---:|---:|---:|
| `int16` 幅度/功率 | 2 | 6.554 MB/s | 32.768 MB/s | 26.214 MB/s | 131.072 MB/s |
| `int32` 定点或 `float32` | 4 | 13.107 MB/s | 65.536 MB/s | 52.429 MB/s | 262.144 MB/s |
| 复数 `int16 I + int16 Q` | 4 | 13.107 MB/s | 65.536 MB/s | 52.429 MB/s | 262.144 MB/s |
| 复数 `float32 I + float32 Q` | 8 | 26.214 MB/s | 131.072 MB/s | 104.858 MB/s | 524.288 MB/s |

如果 PL 已经完成 FFT 后幅度计算，PS 没有必要接收复数 I/Q。只传输幅度或功率能够减少一半带宽和 PS 计算量。

24 位数据即使有效位只有 24 位，也可能为了 AXI 对齐按 32 位传输，因此预算时必须按实际 `TKEEP` 和 payload 布局计算，不能只看有效位宽。

### 6.4 AXI Stream 理论带宽示例

AXI Stream 理论字节率：

```text
TheoreticalRate = StreamWidthBits / 8 × ClockHz
```

示例：

| AXI Stream | 时钟 | 理论上限 |
|---|---:|---:|
| 64 bit | 100 MHz | 800 MB/s |
| 128 bit | 100 MHz | 1,600 MB/s |
| 128 bit | 200 MHz | 3,200 MB/s |

该值不等于可持续应用吞吐。实际还受 AXI DMA、突发长度、HP/HPC 端口、DDR 仲裁、缓存、驱动调度和内存复制影响。

容量规划建议使用效率系数：

```text
RequiredTheoreticalBandwidth >= DmaRate / efficiency
```

初始保守估算可取 `efficiency = 0.7～0.8`，最终必须用板端实测替换。例如 `65.536 MB/s` 在 80% 效率下至少需要约 `81.92 MB/s` 的理论通道能力。

### 6.5 FFT 产生速率不等于 GUI 帧率

若 PL 每完成一次 FFT 就输出一帧：

```text
FftFrameRate = DdcSampleRate / HopSize
HopSize = FFTSize × (1 - OverlapRatio)
```

假设仅作为计算示例：DDC 输出 `245.76 MS/s`、FFT 为 `16,384` 点、无重叠，则：

```text
FftFrameRate = 245.76e6 / 16384 = 15,000 帧/s
float32 数据率 = 16384 × 4 × 15000 = 983.04 MB/s
```

这远高于 GUI 所需的 30～60 FPS，也明显高于当前 1,000 FPS 软件压力基线。若使用 50% 重叠，输出帧率还会翻倍。

因此 PL 不能在未经预算的情况下把每次 FFT 结果全部送到 PS。应根据系统目标选择：

- 在 PL 内进行多帧平均后限速输出；
- 在 PL 内做 Max Hold/Peak Hold 后限速输出；
- 按固定周期抽取完整频谱帧；
- 对触发事件使用独立事件帧或记录通道；
- 将常规显示帧率限制在 PS 能持续处理的范围。

如果系统要求高概率截获短瞬态，不能只依靠 30 FPS GUI。应在 PL 内做峰值保持、触发或事件检测，再把摘要或触发结果送给 PS。

## 7. 当前模拟速率和真实 DMA 速率的关系

### 7.1 可以直接比较的部分

当真实 PL 输出也是 4 字节/点时：

| 场景 | 模拟器 | 真实 DMA |
|---|---:|---:|
| 16,384 点、200 FPS | 13.107 MB/s | 13.107 MB/s 加帧头/对齐 |
| 16,384 点、1,000 FPS | 65.536 MB/s | 65.536 MB/s 加帧头/对齐 |
| 65,536 点、200 FPS | 52.429 MB/s | 52.429 MB/s 加帧头/对齐 |

当前 `16,384 × 1,000 FPS` 模拟测试证明了 PC 版源、处理管线和最新帧发布可以处理约 `65.54 MB/s` 的内部频谱值。

### 7.2 不能由模拟器证明的部分

| 项目 | 模拟源 | 真实 DMA |
|---|---|---|
| 数据产生位置 | CPU 内存中直接生成 | PL 经 AXI 写入 PS DDR |
| 缓存状态 | 通常为 CPU 新写、缓存较热 | 可能为 DMA 写、CPU 冷读或需 cache sync |
| 驱动开销 | 无 | 中断、poll、ioctl、mmap、队列管理 |
| 缓冲所有权 | `FramePool` 直接管理 | 驱动、DMA 和应用三方切换 |
| 数据格式 | 已是 `float32 dBFS` | 可能为 16/32 位定点、功率或复数 |
| 字节序/对齐 | 本机格式 | 必须按协议解析 |
| 半帧和 TLAST | 不存在 | 必须检测短帧、长帧和错位 |
| 溢出和复位 | 软件故障注入 | 真实 DMA 状态寄存器和驱动恢复 |
| 时间戳 | PS 生成 | 可能来自 PL 时钟，需定义时基 |
| 板端 CPU | Windows PC | ZU47DR PS AArch64，能力不同 |

因此当前结果是“负载量级参考”，不能当作板端 DMA 性能验收结果。

### 7.3 两者谁更快

不能简单断言真实 DMA 一定比模拟器快或慢：

- 模拟器需要 CPU 为每个频点生成噪声和信号，产生端 CPU 成本较高；
- DMA 的频谱计算在 PL 完成，PS 不承担模拟生成成本；
- DMA 会增加 DDR 写入、驱动、中断、缓存同步和格式转换成本；
- 如果 PL 直接输出 `float32 dBFS`，PS 只需校验和复制，处理可能较轻；
- 如果 PL 输出线性功率，PS 对每个点执行 `log10`，CPU 成本会显著增加；
- 如果错误追求零拷贝并长期占用 DMA 缓冲，可能导致环形缓冲耗尽。

最终答案只能由目标板上同一点数、格式和帧率的基准测试给出。

## 8. 推荐的 PL 输出定义

### 8.1 第一版推荐格式

第一版联调优先保证简单、确定和可诊断，建议：

- 每个频点一个 32 位字；
- 频点按从低频到高频排列；
- PL 完成 FFT Shift；
- PL 输出对数功率或幅度的有符号定点 dBFS；
- 建议定点比例为 `dBFS × 256`，即 1 LSB = 1/256 dB；
- PS 转换公式为 `floatDbfs = rawInt32 / 256.0f`；
- 每个 DMA 缓冲正好包含一帧头和一帧频谱；
- `TLAST` 只出现在完整频谱帧最后一个 AXI beat；
- 默认小端字节序；
- 起始版本先不压缩、不交错多个通道。

选择 4 字节而不是立即使用 16 位的理由：

- 与当前内部 `float32` 单点大小一致，已有 65.54 MB/s 基线；
- AXI 和 CPU 对齐简单；
- 动态范围和缩放余量充足；
- 协议和转换更容易检查；
- 第一阶段先消除协议风险，后续确有带宽压力再引入 16 位格式。

如果 PL 已经方便输出 IEEE754 `float32 dBFS`，第一版转换可以退化为校验加 `memcpy`。是否让 PL 使用浮点资源，应由 PL 资源和时序报告决定。

### 8.2 必须冻结的频谱语义

必须书面明确：

1. 输出是幅度、功率、功率谱密度还是复数 FFT；
2. dB 计算使用 `20 log10` 还是 `10 log10`；
3. FFT 是否除以 N 或 N²；
4. 窗函数类型和 coherent gain 修正；
5. 窗函数 ENBW；
6. 单 bin 值代表 bin 内功率还是每 Hz 功率密度；
7. 0 dBFS 对应的数字量定义；
8. FFT Shift 是否完成；
9. DC 位于哪个索引；
10. 频率区间是半开 `[Start, Stop)` 还是包含 Stop；
11. 实数 FFT 是否只输出半谱，还是 DDC 后复数 FFT 输出全谱；
12. 溢出、饱和和无效值编码。

当前信道功率算法把每个 dB bin 转为线性功率后求和，因此最适合接收“每 bin 功率”。如果 PL 输出 PSD（例如 dBFS/Hz），应用必须额外乘以 RBW/ENBW；当前 `SpectrumMetadata` 尚无 `rbwHz` 和 `valueKind`，需要扩展后才能保证物理意义正确。

### 8.3 建议的 64 字节帧头

| 偏移 | 字节 | 字段 | 建议类型 | 说明 |
|---:|---:|---|---|---|
| 0 | 4 | `magic` | `uint32` | 固定魔数 |
| 4 | 2 | `protocolVersion` | `uint16` | 首版为 1 |
| 6 | 2 | `headerBytes` | `uint16` | 首版为 64 |
| 8 | 4 | `totalBytes` | `uint32` | 帧头加载荷总长度 |
| 12 | 4 | `flags` | `uint32` | Shift、溢出、标定等 |
| 16 | 8 | `sequence` | `uint64` | 单调递增序号 |
| 24 | 8 | `timestampNs` | `uint64` | 明确定义时基 |
| 32 | 8 | `centerFrequencyHz` | `uint64` | 帧对应中心频率 |
| 40 | 8 | `spanHz` | `uint64` | 帧对应频宽 |
| 48 | 4 | `binCount` | `uint32` | 频点数 |
| 52 | 4 | `payloadBytes` | `uint32` | 载荷有效字节数 |
| 56 | 2 | `sampleFormat` | `uint16` | 频点格式枚举 |
| 58 | 2 | `amplitudeUnit` | `uint16` | dBFS/dBm/线性功率等 |
| 60 | 4 | `configurationEpoch` | `uint32` | 配置版本 |

CRC、RBW、采样率、通道号、触发信息和标定版本可以通过增加 `headerBytes` 在后续协议版本扩展。解析器必须按 `headerBytes` 跳过已知版本允许的扩展，而不能把 C++ 结构体直接强制转换到 DMA 内存。

### 8.4 建议的 sampleFormat

| 值 | 格式 | PS 处理 |
|---:|---|---|
| 1 | `S16_DB_Q7` | `float = int16 / 128.0` |
| 2 | `S32_DB_Q8` | `float = int32 / 256.0` |
| 3 | `F32_DBFS` | 有限值校验后复制 |
| 4 | `U32_LINEAR_POWER` | 归一化并执行 `10 log10` |

第一版只实现并验收一种格式，避免联调期间维护多条未经验证的路径。

### 8.5 建议的 flags

- `FFT_SHIFTED`；
- `CALIBRATED`；
- `PL_OVERFLOW`；
- `ADC_CLIPPED`；
- `TRIGGERED`；
- `DISCONTINUITY`；
- `CONFIG_CHANGED`；
- `TIMESTAMP_VALID`。

保留位必须为 0。未知关键标志默认拒绝或降级为未校准，不得静默忽略。

## 9. PL 数据面和控制面实施建议

### 9.1 数据面

PL 应完成：

1. DDC 和抽取；
2. 固定的窗口函数；
3. FFT 和 FFT Shift；
4. 明确定义的幅度/功率归一化；
5. 必要的平均、峰值保持或输出限速；
6. 帧头生成；
7. 帧序号和配置 epoch；
8. AXI Stream 输出；
9. `TLAST` 完整帧边界；
10. 溢出和错误计数寄存器。

在连接 DMA 前，使用 ILA 验证：

- 首 beat 字节排列；
- 帧头和 payload 长度；
- TKEEP；
- TLAST 位置；
- 连续帧间隔；
- backpressure 下 TVALID/TDATA 稳定；
- DMA 停止时 PL 不覆盖错误状态。

### 9.2 输出限速

建议 PL 暴露：

- `outputEveryNTransforms`；
- `averageCount`；
- `peakHoldCount`；
- `outputFrameRateLimit` 或等价配置；
- 实际输出帧计数和因限速省略的 FFT 计数。

常规显示建议从 `16,384 点、200 FPS` 开始联调，再升到目标值。不要第一步就启用最大频点和最高帧率。

### 9.3 控制面

建议通过 AXI-Lite 寄存器加内核驱动 ioctl，或由已有统一控制驱动提供：

- Center/NCO 频率；
- DDC 抽取率；
- FFT 点数；
- Span/有效采样率；
- 窗函数；
- 平均/峰值保持；
- 输出帧率；
- Start/Stop；
- 配置提交；
- `requestedEpoch` 和 `appliedEpoch`；
- PL 状态、溢出和错误计数。

配置推荐采用 shadow registers：先写完整配置，再写一次 `COMMIT`。PL 在安全帧边界原子生效并更新 `appliedEpoch`。后续每帧头携带该 epoch。

## 10. AXI DMA 和 Linux 驱动方案

### 10.1 DMA 模式选择

建议：

- 固定单帧和最初 bring-up 可使用 Simple 模式验证；
- 连续高帧率正式版本优先使用 Scatter-Gather 或驱动管理的循环缓冲；
- 不建议生产版用户态通过 `/dev/mem` 直接编程 DMA；
- 不建议让 Qt GUI 线程直接执行 DMA ioctl 或阻塞等待。

1,000 FPS 意味着平均每 1 ms 完成一帧。逐帧重新配置 DMA 虽可能工作，但更容易受到调度延迟影响；预先提交多个描述符和缓冲更稳健。

### 10.2 环形缓冲数量

缓冲数下限可估算为：

```text
BufferCount >= ceil(WorstSchedulingLatency × FrameRate) + SafetyMargin
```

例如 1,000 FPS、最坏用户态调度延迟 10 ms，仅覆盖延迟就需要 10 个缓冲。建议初始使用 16 或 32 个缓冲，再依据“最低空闲缓冲数”和板端内存压力调整。

最大缓冲容量：

```text
BufferBytes >= alignUp(HeaderBytes + MaxBinCount × BytesPerBin, DMAAlignment)
```

以 65,536 点、4 字节/点、64 字节头为例，未对齐大小为 `262,208` 字节。

### 10.3 推荐驱动职责

Linux 驱动负责：

- 通过 DMAengine 或平台 DMA API 管理 AXI DMA；
- 申请 coherent DMA 缓冲或正确执行 cache sync；
- 建立循环缓冲和描述符；
- 中断处理和完成队列；
- `poll/epoll` 可等待通知；
- 向用户态返回缓冲索引、有效长度、状态和时间；
- 接收用户态归还缓冲；
- Stop 时取消等待；
- 超时、溢出和硬件错误分类；
- Reset 和重新初始化；
- 驱动级统计计数；
- 多进程互斥和设备权限。

缓存一致性责任应在驱动中完成。上位机不应直接猜测物理地址，也不应自行调用与平台相关的 cache flush/invalidate。

### 10.4 建议的用户态 ABI

建议提供字符设备，例如：

```text
/dev/rtsa-spectrum0
```

建议操作：

- `GET_CAPABILITIES`：点数、格式、最大帧、缓冲数、协议版本；
- `MAP_BUFFERS`：一次 mmap 环形缓冲；
- `START`：启动连续接收；
- `DEQUEUE_COMPLETED`：获取完成缓冲索引、长度和状态；
- `QUEUE_FREE`：归还缓冲；
- `STOP`：停止且唤醒阻塞等待；
- `RESET`：清除错误并重建 DMA；
- `GET_STATISTICS`：完成、溢出、超时和错误计数；
- `APPLY_CONFIG`：若控制面也由同一驱动管理。

`DEQUEUE_COMPLETED` 应支持阻塞、超时和 `poll`。关闭文件描述符必须使所有等待在有限时间内退出。

### 10.5 设备权限

正式运行不应要求 root。建议：

- 创建专用用户组，例如 `rtsa`；
- 使用 udev 规则设置设备为 `root:rtsa`、权限 `0660`；
- 应用服务用户加入该组；
- 禁止普通应用直接映射任意物理内存。

## 11. 缓冲所有权和缓存一致性

缓冲状态必须严格遵守：

```text
Free
  -> Submitted to DMA
  -> DMA Owned
  -> Completed
  -> User Read-only
  -> Returned
  -> Free
```

强制规则：

1. DMA 写入期间应用不得读取；
2. 应用持有期间驱动不得重新提交同一缓冲；
3. 非 coherent 内存在交接处由驱动执行正确的同步；
4. 解析失败也必须归还缓冲；
5. Stop/Reset 必须定义已完成和用户持有缓冲的回收方式；
6. GUI 不得直接持有 DMA 缓冲；
7. 慢 GUI 不得阻塞 DMA 环形缓冲归还。

当前 `SpectrumFrame` 使用自有 `std::vector<float>`，不能直接引用外部 DMA 内存。因此第一版推荐：

1. 从 DMA 完成队列取得只读缓冲；
2. 校验帧头和长度；
3. 一次遍历完成字节序、定点转 float、有限值检查；
4. 写入预分配 `FramePool`；
5. 立即归还 DMA 缓冲；
6. 向现有管线发布独立 `SpectrumFrame`。

这会发生一次复制或转换，但所有权最清楚。只有板端实测证明该复制是瓶颈后，才考虑让 `SpectrumFrame` 支持外部存储和带自定义释放器的 `DmaBufferLease`。零拷贝不能以 DMA 缓冲被 GUI 长时间持有为代价。

## 12. PS 应用新增模块

建议增加：

```text
src/dma/IDmaDevice.h
src/dma/LinuxDmaDevice.h
src/dma/LinuxDmaDevice.cpp
src/dma/DmaBufferLease.h
src/protocol/SpectrumWireProtocol.h
src/protocol/SpectrumFrameParser.h
src/protocol/SpectrumFrameParser.cpp
src/protocol/RawSpectrumConverter.h
src/protocol/RawSpectrumConverter.cpp
src/sources/DmaSpectrumSource.h
src/sources/DmaSpectrumSource.cpp
src/sources/ISpectrumHardwareControl.h
```

建议新增测试：

```text
tests/tst_SpectrumFrameParser.cpp
tests/tst_RawSpectrumConverter.cpp
tests/tst_DmaSpectrumSource.cpp
tests/FakeDmaDevice.h
tests/data/protocol-v1-golden-*.bin
```

### 12.1 `IDmaDevice`

只封装 Linux 驱动 ABI：

- `open()`；
- `capabilities()`；
- `start()`；
- `waitForCompleted(timeout)`；
- `returnBuffer(index)`；
- `stop()`；
- `reset()`；
- `statistics()`。

该接口不解析频谱，不引用 QWidget，便于用 `FakeDmaDevice` 在 Windows/Linux PC 上测试状态机。

### 12.2 `SpectrumFrameParser`

解析器只接收：

- `const std::byte*`；
- DMA 有效长度；
- 协议版本能力；
- 输出的只读解析结果。

解析顺序必须是：

1. 实际长度至少覆盖最小头；
2. 读取 magic 和版本；
3. 校验 `headerBytes`；
4. 使用安全乘加计算期望长度；
5. 校验 `totalBytes` 和 `payloadBytes`；
6. 校验点数、格式、单位和保留位；
7. 校验 Center、Span 和配置 epoch；
8. 最后才访问 payload。

不能用 `reinterpret_cast<PackedStruct*>` 直接解析，因为会引入对齐、大小端和未验证长度问题。

### 12.3 `RawSpectrumConverter`

职责：

- 小端读取；
- 16/32 位有符号扩展；
- 定点缩放；
- 线性功率到 dB 转换；
- NaN/Inf/溢出检测；
- 标定偏移；
- 写入预分配 `float` 缓冲。

转换和校验应合并为单次顺序遍历，避免多次扫描大数组。确认热点后可增加 AArch64 NEON 路径，但必须保留标量参考实现和逐点一致性测试。

### 12.4 `DmaSpectrumSource`

职责：

- 实现 `ISpectrumSource`；
- 管理接收线程和生命周期；
- 等待 DMA 完成；
- 调用解析器和转换器；
- 维护源/传输层统计；
- 把完整帧提交给 `SpectrumPipeline`；
- Stop 时取消阻塞等待并 join；
- 将不可恢复故障转换为 `Error`；
- 执行有界重试和 reset。

建议接收循环：

```text
while not stopped:
    result = dma.waitForCompleted(timeout)

    if timeout:
        update timeout statistics
        apply bounded recovery policy
        continue

    lease = result.buffer
    parsed = parser.parse(lease.bytes, lease.validBytes)

    if parsed invalid:
        count protocol error
        return lease
        continue

    frame = framePool.acquire(parsed.binCount)
    convertAndValidate(parsed.payload, frame.bins)
    copy metadata and configuration epoch
    return lease immediately

    if frame valid:
        frameSink(frame)
```

### 12.5 线程模型

第一版建议单接收线程完成等待、解析和转换，结构简单且便于证明所有权正确。

若板端证明转换影响 DMA 缓冲归还，再拆分为：

```text
DMA 接收线程 -> 有界 SPSC 完成队列 -> 转换/发布线程
```

队列必须有固定容量。满载时应明确选择丢弃最旧或最新帧并计数，不允许无界增长。

### 12.6 数据源工厂和命令行

完成后：

- `SpectrumSourceFactory` 在 Linux 且 DMA 功能启用时创建 `DmaSpectrumSource`；
- Windows 选择 `dma` 仍返回明确不支持；
- 增加 `--dma-device /dev/rtsa-spectrum0`；
- 可增加 `--dma-timeout-ms` 和恢复策略参数；
- 构建选项例如 `RTSA_ENABLE_DMA_SOURCE`；
- DMA 专用源文件只在 Linux/PetaLinux 构建。

## 13. UI 参数与 PL 控制的改造

当前频率和 FFT 参数主要配置模拟源。真实 DMA 接入时需要新增通用硬件控制接口，例如 `ISpectrumHardwareControl`，避免 GUI 直接依赖 Linux ioctl。

建议接口能力：

- 查询支持的 FFT 点数；
- 查询 Center/Span 范围和步进；
- 提交完整配置；
- 返回 requested/applied epoch；
- 查询只读硬件状态；
- 返回拒绝原因；
- 支持 Start/Stop/Reset。

UI 行为：

1. 用户修改 Center/Span/FFT；
2. 主窗口构造配置事务；
3. 数据源提交到驱动/PL；
4. UI 显示“配置中”；
5. 收到 `appliedEpoch` 或带新 epoch 的首帧后显示“已生效”；
6. 超时则恢复旧值或显示明确错误；
7. 不允许仅修改坐标而不修改 PL 数据。

真实源下应隐藏模拟信号组，但保留频率、幅度、轨迹、Marker、测量、文件和状态区域。

## 14. 幅度、RBW 和标定

真实频谱能否正确显示不只取决于曲线是否出现，还取决于幅度语义是否正确。

### 14.1 必须取得的数据

- ADC 满量程和输入阻抗定义；
- DDC 增益和缩放；
- FFT 各级缩放或 block exponent；
- FFT 点数；
- 窗函数 coherent gain；
- 窗函数 ENBW；
- 平均方式；
- RFDC、模拟前端和频率相关增益；
- 温度或增益档位相关标定表。

### 14.2 建议元数据扩展

后续 `SpectrumMetadata` 建议增加：

- `sampleRateHz`；
- `rbwHz`；
- `valueKind`：bin power、PSD、amplitude；
- `windowType`；
- `averagingCount`；
- `calibrationVersion`；
- `channelId`；
- `validStartBin`、`validStopBin`。

### 14.3 验证方法

1. 输入已知 CW 信号；
2. 验证频率落点；
3. 改变输入幅度并验证线性关系；
4. 在多个频率点建立幅度误差表；
5. 验证不同 FFT 点数和窗函数；
6. 验证双音功率叠加；
7. 验证噪声源和信道功率；
8. 完成后才把 `calibrated` 置为 true 并显示 dBm。

## 15. 错误分类和恢复策略

至少分别统计：

- DMA 完成帧；
- DMA 字节数；
- 驱动环形缓冲溢出；
- DMA 硬件错误；
- 等待超时；
- magic/版本错误；
- 长度/TLAST 错误；
- 不支持的格式；
- 序号缺口、重复和回退；
- PL overflow/ADC clipping；
- 原始值转换错误；
- 无效频点；
- 管线拒绝；
- 成功发布；
- GUI 跳过显示。

分类不能重复。例如 PL 序号缺口属于源/传输问题，不能同时计入 GUI 跳过。

建议恢复等级：

| 等级 | 例子 | 行为 |
|---|---|---|
| 帧级 | 单帧 magic/长度错误 | 丢弃整帧、归还缓冲、计数 |
| 短暂传输 | 单次等待超时 | 继续等待并限速记录 |
| 可恢复 DMA | halt/overflow | 停止、reset、重建 ring、有限次数重试 |
| 配置错误 | 不支持点数/格式 | 转 Error，等待用户修正 |
| 持续硬件故障 | 连续 reset 失败 | 转 Error，不无限重试 |

日志应按同类错误聚合和限速，避免高速故障占满系统分区。

## 16. PetaLinux 构建和部署

### 16.1 前置输入

必须获得 PetaLinux 2023.2 工程生成的 extensible SDK/sysroot，并确认目标 rootfs 实际 Qt 版本。PetaLinux 版本号本身不能证明 Qt 已安装，也不能推导准确 Qt 小版本。

rootfs 至少需要：

- Qt Core、Gui、Widgets、Concurrent；
- 目标 QPA 插件，例如 linuxfb、eglfs、xcb 或 Wayland 对应插件；
- 可显示中文的字体；
- PNG 图像插件；
- DMA 内核模块和设备树；
- udev 设备权限规则；
- 必要的触摸输入插件。

### 16.2 构建流程

在 PetaLinux 支持的 Linux 主机中：

```sh
source /path/to/environment-setup-aarch64-*-linux

cmake -S . -B build/petalinux-aarch64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake" \
  -DRTSA_BUILD_TESTS=OFF \
  -DRTSA_TARGET_PETALINUX=ON \
  -DRTSA_ENABLE_DMA_SOURCE=ON

cmake --build build/petalinux-aarch64 --parallel
cmake --install build/petalinux-aarch64 --prefix "$PWD/stage"
```

实际 SDK 环境脚本和 toolchain 路径以工程输出为准。

### 16.3 板端启动顺序

1. 加载 DMA 驱动；
2. 确认 `/dev/rtsa-spectrum0` 权限；
3. 使用驱动测试工具接收固定帧；
4. 设置 QPA；
5. 先运行模拟源确认 Qt 环境；
6. 再运行 DMA 源。

示例：

```sh
export QT_QPA_PLATFORM=linuxfb
export QT_QPA_FB_HIDECURSOR=1

./rtsa_app --source simulated --fullscreen
./rtsa_app --source dma --dma-device /dev/rtsa-spectrum0 --fullscreen
```

## 17. 分阶段实施计划

### 阶段 0：接口冻结

输入：PL 设计、DMA 设计、当前协议清单。

工作：

- 填完 PL-01～PL-12 和 DMA-01～DMA-12；
- 冻结频点格式、幅度语义和字节序；
- 冻结帧头、TLAST 和最大帧长；
- 冻结控制寄存器和配置 epoch；
- 计算典型、峰值和极限数据率；
- 决定 Simple/SG 和 ring 大小；
- 生成至少三份黄金二进制帧。

输出：签署后的协议 v1、寄存器表、黄金帧和吞吐预算。

完成标志：任何一方可以仅根据文档独立生成或解析完全相同的字节流。

### 阶段 1：PC 协议解析与转换

工作：

- 实现 wire protocol 常量；
- 实现严格有界解析器；
- 实现首版 sampleFormat 转换；
- 为正确帧和所有错误边界增加单元测试；
- 对解析器执行随机截断和变异测试；
- 将黄金帧转换为 `SpectrumFrame` 并验证频率/幅度。

此阶段不需要目标板，可以在 Windows 完成。

完成标志：黄金帧逐字段、逐频点一致；错误帧全部安全拒绝。

### 阶段 2：驱动 ABI 和 Fake DMA 数据源

工作：

- 定义 `IDmaDevice`；
- 实现 `FakeDmaDevice`；
- 实现 `DmaSpectrumSource` 状态机；
- 模拟阻塞、超时、断流、溢出和 reset；
- 验证 Stop 能取消等待并有限时间退出；
- 将 Fake DMA 帧送入现有 GUI。

完成标志：不连接板卡也能用 DMA 代码路径显示黄金频谱，生命周期和异常测试通过。

### 阶段 3：PL 固定图样和驱动 bring-up

PL 暂不接真实 FFT，先循环输出：

- 递增 ramp；
- 固定常数；
- 指定 bin 的单点峰；
- 多点已知图样；
- 带已知序号和时间戳的帧头。

驱动测试工具先独立验证：

- 设备打开；
- 单帧接收；
- 长度和 TLAST；
- 多缓冲循环；
- 数据不被覆盖；
- Stop、close 和 reset。

完成标志：连续取得逐字节正确的固定帧，应用尚不参与也能验证。

### 阶段 4：应用低速 DMA 联调

起始配置建议：

```text
1,024 点
10 FPS
单通道
单一 sampleFormat
无动态配置切换
```

依次提升到：

```text
4,096 点 × 50 FPS
16,384 点 × 200 FPS
16,384 点 × 1,000 FPS（若为正式目标）
65,536 点 × 200 FPS（若为正式目标）
```

每一步记录 DMA 完成、解析、发布、GUI 跳过、CPU、内存和温度。

完成标志：完整频谱显示正确，Stop/Restart 可靠，名义速率无源/传输丢帧。

### 阶段 5：真实 DDC/FFT 和配置控制

工作：

- 接入真实 FFT 结果；
- 确认 FFT Shift 和 DC；
- 接入 Center、Span、点数、输出速率控制；
- 验证配置 epoch；
- 快速连续切换配置；
- 检查旧帧不会套用新坐标；
- 验证 Average/Max Hold/Marker 和 CSV。

完成标志：UI 参数、PL 配置和每帧元数据始终一致。

### 阶段 6：性能和过载

工作：

- 测试典型、峰值和极限数据率；
- 测试不同 DMA ring 数；
- 分析驱动中断率、CPU 和 DDR 带宽；
- 比较一次复制与可选少拷贝；
- 测试 GUI 30/60 FPS；
- 注入消费者变慢和系统负载；
- 验证过载只产生有界丢帧，不增加无界延迟。

完成标志：冻结的名义工作点连续运行无 DMA 溢出，过载行为符合设计。

### 阶段 7：幅度和功率标定

工作：

- 标准信号源输入；
- 多频点、多幅度、多 FFT 点数测试；
- 窗函数与 RBW 验证；
- 生成标定表和版本；
- 验证 dBFS、dBm、Marker 和信道功率。

完成标志：幅度和功率误差达到产品指标，帧可标记为 calibrated。

### 阶段 8：系统稳定性

工作：

- 24 小时及更长连续运行；
- 反复启动/暂停/停止；
- PL 断流和恢复；
- DMA reset；
- 应用重启；
- 日志轮转；
- 内存、文件描述符、线程、CPU 和温度趋势；
- 掉电/重启后的自动恢复策略。

完成标志：无死锁、无持续资源增长、错误可诊断且恢复符合预期。

## 18. 测试矩阵

### 18.1 协议正确性

- 正确 magic/version；
- 错误 magic；
- 旧版本和未来版本；
- 头长过小/过大；
- totalBytes 溢出；
- payloadBytes 与点数不匹配；
- 0 点、最大点和超最大点；
- 短帧、长帧、TLAST 提前/缺失；
- 大小端反转；
- 未知 sampleFormat/unit/flags；
- 序号跳变、重复、回退和回绕；
- 配置 epoch 切换；
- 非有限 float 和定点饱和。

### 18.2 DMA 生命周期

- 单次采集；
- 连续采集；
- 暂停/恢复；
- Stop 取消阻塞等待；
- 无数据超时；
- 缓冲耗尽；
- 驱动 overflow；
- DMA halt；
- reset 成功/失败；
- 关闭应用时仍有缓冲完成；
- 重复启动 1,000 次。

### 18.3 数据正确性

- ramp 对应每个 bin；
- 单点峰频率；
- 第一 bin、DC bin、最后有效 bin；
- 双音幅度；
- 不同 FFT 点数；
- FFT Shift 开/关保护；
- Center/Span 改变；
- dB 定点转换；
- 窗增益和 RBW；
- CSV 与 DMA 原始帧逐点对比。

### 18.4 性能和长稳

- 1,024/4,096/16,384/65,536 点；
- 10/50/200/1,000 FPS；
- 2/4/8 字节每点；
- 8/16/32 个 DMA 缓冲；
- GUI 30/60 FPS；
- Average/Max Hold；
- PNG/CSV 同时操作；
- CPU/DDR 压力背景负载；
- 24 小时资源趋势。

## 19. 建议验收指标

最终数值应在协议冻结后签署。第一轮可使用以下门槛：

| 类别 | 建议门槛 |
|---|---|
| 正确性 | 黄金帧逐点一致，无半帧或撕裂 |
| 名义吞吐 | 冻结工作点连续 1 小时，DMA/驱动溢出为 0 |
| GUI | 1920×1080 持续显示不低于 30 FPS |
| 可见延迟 | P95 小于 150 ms，时间戳时基已校准 |
| Stop | 阻塞等待可取消，应用在规定时间内退出 |
| 内存 | 长稳期间无持续线性增长 |
| 丢帧 | 源、协议、处理、GUI 分类互不重复 |
| 恢复 | 可恢复 DMA 错误能有界 reset；持续故障转 Error |
| 标定 | 按产品定义的频率/幅度误差验收 |

GUI 跳过帧不等于 DMA 丢帧。输入 1,000 FPS、GUI 显示 30～60 FPS 时，GUI 有意只显示最新帧；只要 DMA 和处理层完成情况符合策略，这属于正常设计。

## 20. 主要风险和对策

| 风险 | 后果 | 对策 |
|---|---|---|
| PL 输出语义未冻结 | 幅度和功率结果错误 | 协议签署和黄金帧先行 |
| FFT 输出速率按采样率自然产生 | DMA 数据率远超预算 | PL 平均、保持、抽取和限速 |
| TLAST 不是完整帧尾 | 短帧、错位和跨帧 | 一帧一 TLAST，ILA 和错误注入验证 |
| 用户态直接控制物理 DMA | 缓存和安全问题 | 使用内核驱动管理 DMA/缓存 |
| 过早做零拷贝 | 缓冲被 GUI 长时间持有 | 第一版复制到 FramePool |
| 输出线性功率由 PS 做 log10 | AArch64 CPU 负载过高 | 优先由 PL 输出对数定点，或 NEON/LUT 实测优化 |
| Center/Span 配置与帧不同步 | 坐标错误 | shadow config + applied epoch |
| PSD 与 bin power 混淆 | 信道功率错误 | 明确 valueKind、RBW 和 ENBW |
| DMA 中断过于频繁 | CPU 抖动和丢帧 | SG ring、中断合并或批量 dequeue |
| Qt 5/rootfs 依赖缺失 | 板端无法启动 | 先用模拟源验证 SDK/QPA/rootfs |
| 日志记录每个错误帧 | 系统分区被写满 | 限速日志和累计统计 |

## 21. 三方需要立即提供的输入

### 21.1 PL 负责人

- DDC 输出采样率和抽取档位；
- FFT 点数、重叠率和窗口；
- FFT 输出帧率和限速方案；
- FFT Shift/DC 索引；
- 幅度/功率计算及缩放；
- 每点位宽、定点比例、字节序；
- AXI Stream 宽度、时钟、TKEEP、TLAST；
- 帧头和配置 epoch；
- 溢出和状态寄存器；
- 黄金帧及对应理论频谱。

### 21.2 Linux 驱动负责人

- AXI DMA Simple/SG 选择；
- 设备树和 DMA channel；
- 缓冲数、大小和对齐；
- coherent/cache sync 方案；
- mmap、poll、ioctl ABI；
- timeout、cancel、stop、reset 语义；
- 设备节点和权限；
- 驱动统计；
- 独立 DMA 接收测试工具。

### 21.3 PS 应用负责人

- 协议解析器和转换器；
- `IDmaDevice` 和 `DmaSpectrumSource`；
- PL 控制接口；
- 统计和错误映射；
- Qt 5/AArch64 构建；
- GUI、CSV 和标定验证；
- 性能与长稳报告。

## 22. 推荐的下一步执行顺序

1. PL、驱动和 PS 三方召开一次协议冻结评审；
2. 填完《DMA 数据源接口与 PL/PS 协议确认清单》；
3. 明确典型工作点，例如 `16,384 点、200 FPS、S32_DB_Q8`；
4. 明确峰值工作点，例如 `16,384 点、1,000 FPS`；
5. 明确是否真的需要 `65,536 点、1,000 FPS` 极限场景；
6. PL 生成固定 ramp、单峰和双峰黄金帧文件；
7. PS 先在 Windows 实现并测试协议解析器；
8. 驱动提供环形缓冲和独立接收工具；
9. 板端先用固定图样完成 DMA bring-up；
10. 再把 `DmaSpectrumSource` 接入现有 Qt 软件；
11. 低速正确后逐级升速；
12. 最后接真实 DDC/FFT、配置控制和幅度标定。

不要把“驱动刚能收到字节”和“频谱系统完成”视为同一里程碑。必须依次证明字节正确、帧正确、频率正确、幅度正确、性能稳定和故障可恢复。

## 23. 最终完成定义

满足以下条件后，才能认为最初的 ZU47DR PL/AXI DMA 频谱上位机目标完成：

- PL/PS 协议已版本化并签署；
- DMA 驱动具备安全缓冲所有权和取消/复位语义；
- `--source dma` 能创建真实数据源；
- DMA 频谱与黄金数据逐点一致；
- UI 参数能够原子配置 PL 并通过 epoch 确认；
- 频率轴、FFT Shift、DC 和幅度缩放正确；
- dBFS/dBm、RBW 和信道功率语义经过标定；
- 名义数据率无源/传输丢帧；
- 过载时队列有界、统计明确、显示保持最新状态；
- 目标 PetaLinux Qt 环境可直接启动；
- 目标屏幕和触摸操作正常；
- Stop、重启、断流和 DMA reset 可恢复；
- 板端性能和 24 小时长稳验收通过；
- 发布包、驱动、设备树、配置、日志和操作说明完整。

## 24. 相关文档

- 《实时频谱分析仪软件需求规格说明书》；
- 《实时频谱分析仪软件总体实现架构与设计方案》；
- 《DMA 数据源接口与 PL/PS 协议确认清单》；
- 《需求追踪矩阵》；
- 《项目实现与验证报告》；
- 《实时频谱分析仪上位机功能介绍与使用说明》。
