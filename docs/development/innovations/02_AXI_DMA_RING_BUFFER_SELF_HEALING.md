# 专题 02：AXI DMA 环形缓冲、溢出回填与消费限流

## 1. 文档范围

本文记录 `driver/DMA` 与 `DmaSpectrumSource` 当前如何处理物理槽位、完成队列溢出、用户态取帧和发布节流。

| 项目 | 状态 |
|---|---|
| 128 个 132 KiB 槽位、`frame_size`、`mmap`、`poll`、`ioctl`、单轮 64 帧上限 | 已实现 |
| 完成队列满时丢弃当前帧并重新提交该 slot | 已实现 |
| 真实 PL 突发、DMA 吞吐、缓存一致性和故障恢复 | 板端待验 |
| PL V1 帧载荷、绝对幅度、完整丢帧计数 | 板端/标定待确认 |

“自愈”在本项目中仅指：完成队列满时，驱动不让刚完成的 slot 永久脱离硬件接收流，而是立即重新提交。已经因队列满被丢弃的数据帧无法恢复，因此不能据此宣称零丢帧或永不拥塞。

## 2. 缓冲布局

UAPI 定义：

```text
RTSA_RING_BUFFER_COUNT = 128
RTSA_SLOT_SIZE         = 132 KiB
映射区总大小            = 约 16.5 MiB
当前频谱帧长            = 2112 B
```

每个物理槽位是一个 DMA 接收目标。内核用环形方式提交所有槽位，并用单独的完成队列记录待用户态读取的 slot。完成队列保留一个空位区分满和空，因此其可排队容量不是 128，而是 127 项。

## 3. 驱动侧流程

```text
PL AXI-Stream
  -> dmaengine 接收描述符
  -> rtsa_dma_rx_callback()
  -> 记录 sequence / timestamp / size / status
  -> 完成队列未满：slot 入队并唤醒 poll
  -> 完成队列已满：当前帧丢弃，slot 标为 FREE 并重新提交
```

溢出路径的关键代码位于 `driver/DMA/rtsa_dma_driver.c`：

- 完成后先获取 residue，推导本次短包的实际长度；
- 计算 `next_head` 并检查是否追上 `completed_tail`；
- 队列满时设置告警、丢弃当前完成结果，并调用 `rtsa_submit_rx_slot()`；
- 正常路径写入 `completed_queue`、更新 head 并唤醒等待队列。

该机制解决的是“槽位不再被重新提交”导致的接收流衰减风险。它不能消除完成队列容量有限带来的瞬时丢帧，也不能替代板端压力测试。

## 4. 用户态取帧与削峰

`DmaSpectrumSource` 的 Linux 路径执行：

1. `open()` 设备节点；
2. `mmap()` 整个约 16.5 MiB 槽位区；
3. `RTSA_IOC_SET_FRAME_SIZE = 2112`，然后 `RTSA_IOC_START` 启动接收；
4. `poll()` 等待 DMA 事件；Linux 下同时监听 `eventfd` 以响应参数/停止请求；
5. 单轮最多通过 `RTSA_IOC_DEQUEUE` 取 64 个完成项；
6. 对每个完成项及时执行 `RTSA_IOC_QUEUE` 归还 slot；
7. 按约 60 FPS 的间隔选择一项复制 V1 帧并由 `SpectrumFrameCodec` 解析后发布。

`kMaxBatchPerPoll = 64` 限制单次事件处理量，避免持续排空时长期不返回外层循环。停止请求仍会在内层循环条件中检查。只有单轮达到 64 项且没有安排发布时，才主动执行 `std::this_thread::yield()`。

这里的“及时归还”是工程措施，不是经过板端测量后的固定延迟指标。文档不使用“纳秒级归还”作为验收结论。

## 5. 帧转换与发布

发布路径由 `SpectrumFrameCodec` 解析 64 B V1 帧头 + 1024 × `int16`：

```text
magic / version / frame_bytes / bin_count 校验
payload int16，0.01 dB/LSB
```

帧元数据来自载荷内帧头：

- `centerFrequencyHz`
- `spanHz`
- `binCount`
- `traceMode` / `accumulationFrames` / `windowType`
- `calibrationOffsetQ16_16`
- `configEpoch` / `sequence` / `timestampTicks`

发布间隔默认 `uiRefreshRateHz = 60`，首次满足间隔的完成项被复制；其余完成项只计数并归还。`producedFrames_` 表示已经从完成队列取出的项数，不等同于实际送给界面的帧数。

## 6. 当前统计与协议缺口

当前实现已经提供：

- `RTSA_IOC_GET_STATS` 累计完成帧/字节、队列溢出、短帧和 DMA 错误；
- `info.status` 的 bit0/bit1/bit2 状态；
- V1 帧头校验、`info.size` 检查和 `config_epoch` 过滤；
- 短帧和解析失败时归还槽位。

仍需板端确认：真实溢出率、`frame_size=2112` 的 TLAST 行为、132 KiB 槽位余量和跨线程并发稳定性。

## 7. 历史记录与证据边界

旧文档记录过约 `8163.9 FPS` 的 benchmark 数字。该记录缺少当前 commit、PL bit、板卡启动参数、测点、原始日志和重复次数，本文仅把它视为早期线索，不作为当前吞吐或零丢帧证据。

板端验收建议记录：

| 项目 | 必填信息 |
|---|---|
| 输入速率 | PL 名义帧率、短包长度、突发模式 |
| 用户态接收 | 每秒 dequeue 数、每轮批次分布、队列等待时间 |
| 丢帧 | 驱动溢出计数、用户态状态计数、序号缺口 |
| 恢复 | 停止/启动、模块重载、PL reset、应用重启 |
| 资源 | CPU、内存、DMA 映射、温度和长时间趋势 |
| 数据正确性 | 序号、时间戳、`info.size`、载荷边界和已知输入比对 |

## 8. 相关文档

- [DMA 数据源接口与 PL/PS 协议](../../design/DMA数据源接口与PL_PS协议确认清单.md)
- [ZU47DR PL/AXI DMA 接入差距与实施方案](../../design/ZU47DR_PL_AXIDMA频谱数据接入差距分析与后续实施方案.md)
- [DMA 驱动构建与板端部署记录](../DMA驱动解析.md)
