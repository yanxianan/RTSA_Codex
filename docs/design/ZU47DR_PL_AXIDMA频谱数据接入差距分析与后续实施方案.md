# ZU47DR PL/AXI DMA 频谱数据接入差距分析与后续实施方案

| 项目 | 内容 |
|---|---|
| 文档版本 | V0.3 |
| 文档状态 | 当前实现状态 + 后续差距分析 |
| 最近修订 | 2026-11-06 |
| 当前结论 | 1024 点 V1 帧、UIO 控制面、132 KiB DMA 环和 312.5 MHz 旁路已实现；板端性能、标定和长稳待验 |

## 1. 文档目的

本文分析 ZU47DR PL 频谱结果通过 AXI DMA 进入 PS 端 Linux，再由 Qt 应用显示的剩余差距。内容覆盖：

- 当前已经实现的软件和数据路径；
- 尚未冻结的 PL/PS 协议与驱动契约；
- 数据率估算和测试边界；
- 板端 bring-up、性能、标定和长稳验收顺序。

本文不把源码存在等同于板端完成。任何“已实现”描述都仅指当前仓库路径，“已验证”必须附环境、版本、参数和记录。

> 2026-11-06 状态说明：本文早期版本中的“裸 int32”“256 KiB 槽”“无帧头”假设已经废弃。当前协议以 [DMA 数据源接口与 PL/PS 协议](DMA数据源接口与PL_PS协议确认清单.md) 和 [PL/PS 频谱接口设计](PL_PS_Spectrum_Interface_Design_v1.md) 为准。

## 2. 结论摘要

当前仓库已经具备：

- Qt 频谱和瀑布界面；
- 模拟源与 Linux `DmaSpectrumSource`；
- `SpectrumPipeline` 同步处理和 `LatestFrameStore` 最新帧发布；
- dmaengine 驱动、128 × 132 KiB coherent DMA 环（约 16.5 MiB）和 `/dev/rtsa_dma` 设备；
- `open`、`mmap`、`poll`、`ioctl` 的 UAPI，包括 `SET_FRAME_SIZE` 和 `GET_STATS`；
- `--source dma` 命令行选择；
- 设备树示例和板端构建/验证命令记录。
- 64 B 版本化帧头 + 1024 × `int16` 对数幅度，V1 帧长 2112 B；
- `rtsa_regs` UIO 控制面，运行中 staging + commit，DMA 不停机；
- `CIC=1/HB=0` 的 312.5 MHz CIC 旁路模式。

当前仍缺少的最终条件：

- 板端对 V1 帧头、TLAST、`frame_size` 和短帧状态的实际验证；
- 真实板端 DMA、DDR、缓存一致性和丢帧验证；
- Qt 5.15.x/PetaLinux SDK/AArch64/linuxfb 验收；
- 绝对幅度、RBW、PSD 和信道功率标定；
- 24 小时长稳、断流恢复和端到端延迟结论。

## 3. 目标数据流

```text
RFDC / ADC
  -> PL DDC 与抽取
  -> 加窗
  -> FFT
  -> 可选 FFT Shift
  -> 对数幅度 + 64 B V1 帧头
  -> AXI4-Stream
  -> AXI DMA S2MM
  -> 128 x 132 KiB coherent DMA 环
  -> dmaengine 驱动 / /dev/rtsa_dma
  -> DmaSpectrumSource
  -> SpectrumFrameCodec 解析 int16 dBFS
  -> SpectrumPipeline
  -> LatestFrameStore
  -> Qt Raster 频谱与瀑布
```

正式系统还需要控制路径：

```text
Qt 参数
  -> PlRegisterMap staging
  -> 0x120 COMMIT
  -> PL 帧边界锁存 active 配置
  -> 配置生效 epoch
  -> 后续频谱帧可追溯
```

该控制闭环已经实现；剩余是板端验证其稳定性。

## 4. 当前实现基线

### 4.1 应用侧

模拟源和 DMA 源都实现 `ISpectrumSource`。采集线程通过 `FrameSink` 直接调用 `SpectrumPipeline::submit()`，同步完成帧校验、轨迹处理和最新帧发布。没有独立处理线程或 SPSC 处理队列。

`DmaSpectrumSource` 当前行为：

- 设备默认 `/dev/rtsa_dma`；
- 固定 1,024 bins，硬件 span 由 CIC/HB 决定；
- 解析 64 B V1 帧头 + 1024 × `int16` 对数幅度；
- 运行中通过 `rtsa_regs` staging + commit 下发参数；
- 默认关闭 FFT Shift，帧头解析后 metadata 标记 FFT shift；
- 每轮 `poll` 最多排空 64 个槽；
- UI 发布节流约 60 FPS；
- 标记 `calibrated=false`；
- 单次采集在发布一个有效帧后停止；
- 帧池上限为 64。

### 4.2 驱动侧

`driver/DMA/rtsa_dma_driver.c` 当前：

- compatible 为 `rtsa,spectrum-dma`；
- 请求名为 `rx_chan` 的 dmaengine 通道；
- 使用 `dma_alloc_coherent` 分配 128 × 132 KiB，共约 16.5 MiB；
- 通过 `frame_size` 按实际帧长提交 DMA，当前频谱帧 2112 B；
- 每槽完成事件通过状态队列通知用户态；
- 完成队列溢出时重新提交槽位，避免硬件请求链永久中断；
- 通过 `/dev/rtsa_dma` 提供 START、STOP、DEQUEUE、QUEUE、SET_FRAME_SIZE 和 GET_STATS。

上述机制需要板端验证。驱动注释中的“彻底杜绝丢帧”等表述不是验收证据。

### 4.3 当前协议边界

当前 DMA 载荷是版本化 V1 频谱帧：

- Magic/version/header bytes/frame bytes；
- sequence 和 timestamp；
- Center/Span/bin count/trace/accum/window；
- calibration、data format、unit、config epoch；
- 1024 × little-endian `int16` 对数幅度。

用户态通过 `SpectrumFrameCodec` 校验并解析；驱动按 `frame_size` 和实际完成字节返回状态。详细字段见 [DMA 数据源接口与 PL/PS 协议](DMA数据源接口与PL_PS协议确认清单.md)。

## 5. 差距矩阵

| 层级 | 当前状态 | 还缺什么 | 完成条件 |
|---|---|---|---|
| PL DDC/FFT | 外部实现 | 输出语义、位宽、顺序、帧率和限速 | 版本书面协议与黄金帧 |
| AXI4-Stream | 外部实现 | `TKEEP`、`TLAST`、帧间隔 | ILA 与 DMA 帧边界一致 |
| AXI DMA | 驱动基线存在 | 实际模式、吞吐、错误恢复验证 | 板端连续 S2MM 测试通过 |
| 设备树 | 示例存在 | 实际节点、时钟、中断和资源核对 | probe 成功且 DMA 通道正确 |
| Linux 驱动 | 实现基线存在 | UAPI 冻结、取消、复位、边界检查 | 非 root 应用可稳定运行 |
| 用户态源 | 已实现 | `info.size` 契约、版本化解析 | 单帧/连续/错误注入通过 |
| PL 控制面 | 已实现 | 板端验证运行中重配稳定性 | 配置 epoch 可追溯 |
| 幅度标定 | 未完成 | 满量程、窗增益、FFT 归一化 | 标准源比对满足指标 |
| 功率语义 | 未完成 | PSD 或 bin 功率、RBW/ENBW | 信道功率有物理意义 |
| Qt 5/AArch64 | 未验证 | SDK、交叉构建、QPA、部署依赖 | 目标程序启动并交互 |
| 板端性能 | 未验证 | DMA/DDR/CPU/显示实测 | 冻结工作点达到验收指标 |
| 长稳恢复 | 未验证 | 断流、溢出、reset、24 小时 | 无死锁和持续泄漏 |

## 6. 数据率估算

设：

- `N`：每帧频点数；
- `B`：每点字节数；
- `F`：每秒帧数；
- `H`：帧头字节数；
- `A`：对齐和填充字节数。

```text
有效载荷率 = N × B × F
DMA 有效字节率 = (H + N × B + A) × F
```

当前 V1 帧为 64 B 帧头 + 1024 × 2 B：

| 频点数 | 单帧 | 200 帧/s | 1,000 帧/s |
|---:|---:|---:|---:|
| 1,024 | 2112 B | 0.42 MB/s | 2.11 MB/s |
| 4,096 | 8256 B | 1.65 MB/s | 8.26 MB/s |
| 16,384 | 32832 B | 6.57 MB/s | 32.83 MB/s |
| 65,536 | 131136 B | 26.23 MB/s | 131.14 MB/s |

132 KiB 槽可容纳 65,536 点帧（131136 B），但仍需为驱动状态、对齐和异常回退留出余量。当前实际工作是 1024 点 / 2112 B。

## 7. 当前协议与后续扩展

V1 帧头、`TLAST` 帧边界、int16 对数幅度、Center/Span/epoch 已经实现。以下是后续增加点数或改变测量语义时必须继续冻结的内容。

### 7.1 必须冻结的语义

- FFT 点数及运行时变化规则；
- 频点格式：有符号定点、浮点、幅度、功率或对数；
- 每点总位宽、小数位和饱和规则；
- little-endian 或 big-endian；
- 是否完成 FFT Shift，DC 所在索引；
- 一帧与一个 `TLAST` 的关系；
- 帧序号、时间戳、Center、Span 和配置 epoch 来源；
- dBFS、dBm、PSD、RBW 和窗函数修正语义；
- 过载时 PL 丢帧、限速或标记规则。

### 7.2 V1 版本化帧头

V1 已经实现 64 B 帧头，当前字段如下（后续扩展需保持向后兼容）：

| 字段 | 建议类型 | 用途 |
|---|---:|---|
| `magic` | `uint32` | 快速识别错位数据 |
| `protocolVersion` | `uint16` | 选择解析器 |
| `headerBytes` | `uint16` | 后续兼容扩展 |
| `totalBytes` | `uint32` | 总长度 |
| `flags` | `uint32` | Shift、校准、溢出和触发状态 |
| `sequence` | `uint64` | 去重和序号缺口统计 |
| `timestampNs` | `uint64` | 时间基准需另行定义 |
| `centerFrequencyHz` | `uint64` | 中心频率 |
| `spanHz` | `uint64` | 分析带宽 |
| `binCount` | `uint32` | 频点数 |
| `sampleFormat` | `uint16` | 数据格式枚举 |
| `amplitudeUnit` | `uint16` | 单位枚举 |
| `payloadBytes` | `uint32` | 载荷长度 |
| `configEpoch` | `uint32` | 配置一致性 |

若元数据通过寄存器控制面提供，必须定义“元数据读取”和“对应 DMA 帧”之间的一致性机制。

### 7.3 解析器原则

- 先检查实际映射范围和 `info.size`，再读取任何载荷；
- 使用安全乘加检查长度溢出；
- 不把 `reinterpret_cast<PackedStruct*>` 当作跨平台解析方式；
- 拒绝未知版本、未知格式、非法点数和不一致 flags；
- 转换和有限值检查在一次遍历中完成；
- 解析失败也必须归还 DMA 槽；
- 统计协议错误、序号缺口、转换错误和发布丢弃。

## 8. 建议重构边界

当前 `DmaSpectrumSource` 已包含设备访问和转换逻辑。后续可在不改变 UI 的前提下拆为：

```text
IDmaDevice
  -> 负责 open/start/wait/dequeue/queue/stop/reset
SpectrumFrameParser
  -> 负责版本、长度和元数据校验
RawSpectrumConverter
  -> 负责字节序、定点、dB 和标定转换
DmaSpectrumSource
  -> 负责线程、状态机、重试和发布
```

这是重构建议，不是“从零新增 DMA 源”。拆分目的是便于黄金数据测试、错误注入和协议版本演进。

## 9. 推荐验收顺序

### 阶段 1：协议基线确认

- V1 帧头、1024 点、int16 对数幅度已实现；
- 槽大小固定 132 KiB，当前帧长 2112 B；
- 板端确认 `TLAST`、`info.size`、短帧和错误状态；
- 任何点数或格式变化都按新协议版本处理。

### 阶段 2：固定图样 bring-up

- PL 发送 ramp、固定单峰和双峰；
- DMA 只验证槽序号、长度、序号和完成状态；
- 用户态保存原始槽并离线比对；
- 不先接真实 DDC/FFT，避免多因素叠加。

### 阶段 3：用户态正确性

- 校验逐点字节序和转换；
- 注入错误长度、超时、序号跳变和槽状态错误；
- 验证 `SingleFrame`、Stop、取消 poll 和 reset；
- 确认失败路径全部归还槽位。

### 阶段 4：低速到高速

- 先用低 FPS、少点数验证连续运行；
- 逐级提升到冻结的名义工作点和峰值工作点；
- 同时记录 PL 输出、DMA 完成、用户态发布和 GUI 显示统计；
- 区分 PL 丢帧、驱动溢出、用户态转换丢弃和 GUI 跳过显示。

### 阶段 5：控制面与标定

- 下发 Center/Scan 参数并确认 PL 生效；
- 为每帧带上配置 epoch；
- 用标准信号源验证频率、幅度、RBW 和信道功率；
- 在标定完成前持续显示 `calibrated=false`。

### 阶段 6：板端与长稳

- 完成 PetaLinux Qt 5.15.x SDK 和 AArch64 构建；
- 验证 linuxfb/XCB 等目标 QPA、字体和触摸；
- 验证断流、恢复、DMA reset 和应用重启；
- 执行 24 小时长稳，记录内存、CPU、温度和错误计数。

## 10. 测试矩阵

| 类别 | 测试 |
|---|---|
| 协议 | Magic、版本、长度、点数、格式、字节序、帧边界 |
| 转换 | ramp、单峰、双峰、负值、饱和、非法值、FFT Shift |
| 驱动 | START/STOP、等待、QUEUE、超时、溢出、reset |
| 所有权 | 解析失败归还、Stop 时归还、GUI 变慢不持有 DMA 内存 |
| 性能 | 低/名义/峰值 FPS、持续/突发、CPU、内存、延迟 |
| 恢复 | 断流、重新加载 bit、驱动重载、应用重启 |
| 标定 | 已知单音、幅度、RBW、PSD、信道功率 |
| 长稳 | 24 小时连续运行和资源趋势 |

## 11. 完成定义

只有同时满足以下条件，才能宣称 ZU47DR PL/AXI DMA 频谱接入完成：

- PL/PS 协议已版本化并签署；
- 驱动 UAPI 和 `info.size` 契约已冻结；
- `--source dma` 在目标板上创建并稳定运行真实源；
- 黄金帧逐点一致；
- 参数控制和配置 epoch 一致；
- 频率轴、FFT Shift 和幅度语义正确；
- 标定和信道功率具备物理意义；
- 名义和峰值工作点的吞吐、丢帧、延迟可复现；
- Stop、断流、reset 和重启可恢复；
- Qt 5/AArch64/目标 QPA 部署通过；
- 24 小时长稳和发布材料完成。

## 12. 相关文档

- 《实时频谱分析仪软件需求规格说明书》；
- 《实时频谱分析仪软件总体实现架构与设计方案》；
- 《DMA 数据源接口与 PL/PS 协议确认清单》；
- 《DMA 驱动构建与板端部署记录》；
- 《项目实现与验证报告》；
- 《需求追踪矩阵》。
