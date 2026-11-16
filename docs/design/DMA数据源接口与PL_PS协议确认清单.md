# DMA 数据源接口与 PL/PS 协议

| 项目 | 内容 |
|---|---|
| 文档版本 | V0.3 |
| 文档状态 | 当前实现基线；板端待验收 |
| 最近修订 | 2026-11-06 |
| 设备节点 | `/dev/rtsa_dma`、`rtsa_regs` UIO |
| 当前帧格式 | 64 B 版本化帧头 + 1024 × `int16` 对数幅度 |

## 1. 当前链路

```text
RFADC/DDS
→ RFDC + PL 抽取/旁路
→ 1024 点复数 FFT
→ Max Hold
→ 64 B 帧头 + 1024 × int16
→ AXI DMA
→ /dev/rtsa_dma
→ DmaSpectrumSource
→ SpectrumPipeline
→ GUI
```

PS 通过 `rtsa_regs` UIO 控制 PL 参数。DMA 数据面与 UIO 控制面已经解耦：运行中改参数不需要停止 DMA。

## 2. 当前帧格式

固定 1024 点，帧长 2112 B：

```text
offset  size   field
0x00    4      magic = 0x52545341 ("RTSA")
0x04    2      version = 1
0x06    2      header_bytes = 64
0x08    4      frame_bytes = 2112
0x0C    4      flags = 0x00000101 (LOG_FORMAT | FRAME_VALID)
0x10    8      sequence
0x18    8      timestamp_ticks
0x20    8      center_frequency_hz
0x28    8      span_hz
0x30    4      bin_count = 1024
0x34    1      trace_mode
0x35    2      accumulation_frames
0x37    1      window_type
0x38    4      calibration_offset_q16_16
0x3C    1      data_format = 0
0x3D    1      unit = 0 (dBFS)
0x3E    2      config_epoch
0x40    2048   payload: 1024 × little-endian int16
```

载荷为对数幅度，`0.01 dB/LSB`；PS 使用 `SpectrumFrameCodec` 解析，不再按裸 `int32` 处理。`timestamp_ticks` 由 PL 312.5 MHz 计数器产生，PS 换算为 `3.2 ns/tick`。

## 3. UAPI 基线

| 项目 | 当前值 |
|---|---|
| UAPI 版本 | `RTSA_UAPI_VERSION = 2` |
| 槽位数 | 128 |
| 单槽容量 | `RTSA_SLOT_SIZE = 132 KiB` |
| 总映射 | 约 16.5 MiB |
| 默认最大帧长 | `RTSA_MAX_FRAME_SIZE = 131136` |
| 当前频谱帧长 | 2112 B |
| 启动/停止 | `RTSA_IOC_START` / `RTSA_IOC_STOP` |
| 出队/归还 | `RTSA_IOC_DEQUEUE` / `RTSA_IOC_QUEUE` |
| 设置帧长 | `RTSA_IOC_SET_FRAME_SIZE`（停止状态下） |
| 查询统计 | `RTSA_IOC_GET_STATS` |
| 完成通知 | `poll` |

`rtsa_buffer_info.status`：

```text
bit0  完成队列溢出
bit1  短帧（info.size < frame_size）
bit2  DMA 错误
```

应用启动前会调用 `RTSA_IOC_SET_FRAME_SIZE = 2112`。用户态和驱动必须使用同一版本的 `rtsa_dma_uapi.h`。

## 4. 控制面

PL 寄存器：

| 地址 | 用途 |
|---|---|
| `0x100..0x11C` | center/span/trace/accum/window/calibration/epoch |
| `0x120` | COMMIT（写 0→1 触发原子快照） |
| `0x130` | `CONFIG_BUSY` 只读 |
| `0x134` | `ACTIVE_EPOCH` 只读 |
| `0x200..0x224` | DDS/CIC/HB/Max Hold staging |

DDS_RESYNC 和 CIC_VALID 由 PL 内部产生，PS 不直接写。运行中配置在帧边界生效；硬参数只复位 PL 数据通路，DMA 描述符环不停。

## 5. 当前带宽参数

```text
Fs = 312.5 / (CIC_RATE × 2^HB_LEVEL) MSPS
span = Fs            （复数 FFT）
bin 间隔 = Fs / 1024
RBW ≈ bin 间隔 × 1.90（Blackman-Harris）
```

| 模式 | CIC | HB | span | bin | 近似 RBW |
|---|---:|---:|---:|---:|---:|
| 默认 | 8 | 2 | 9.765625 MHz | 9.54 kHz | 18.1 kHz |
| 正常最宽 | 4 | 0 | 78.125 MHz | 76.29 kHz | 145 kHz |
| 宽带直通 | 1 | 0 | 312.5 MHz | 305.18 kHz | 580 kHz |

`CIC=1` 是旁路，不是正常 1 倍抽取。312.5 MHz 模式使用 0% 重叠组帧；组帧控制器可能跳过窗口，`frame_drop_count` 非零不代表 DMA 丢包。

## 6. 驱动与设备树责任

- `driver/DMA/rtsa_dma_driver.c`：按 `frame_size` 提交 DMA，返回实际完成字节和状态，提供统计。
- `driver/DMA/rtsa_dma_uapi.h`：与用户态共享 ABI，必须整体同步。
- `driver/DMA/system-user.dtsi`：`axi_bram_ctrl_0` 必须提供 `linux,uio-name = "rtsa_regs"`。
- 驱动、DTB、bitstream、AArch64 应用必须同版本部署。

## 7. 解析与验收规则

1. 检查 `info.index` 是否在映射范围。
2. 检查 `info.size >= 64`，并按帧头 `frame_bytes` 校验完整帧。
3. 校验 magic、version、bin_count、data_format。
4. 旧 epoch 帧丢弃，等待新 epoch。
5. 解析失败必须归还槽，并累计错误统计。
6. 验收覆盖：DDS 默认 bin 157、RFADC 1.7015 GHz、运行中 CIC/HB/DDS 变更、312.5 MHz 直通、24 小时长稳。

## 8. 变更记录

- V0.2 的“无帧头裸 int32 / 256 KiB 槽”假设已废弃。
- 当前实现为 V1 帧头、`int16` 对数幅度、132 KiB 槽、`frame_size` 可配置。
