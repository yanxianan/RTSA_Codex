# P2 板端联调清单（1024 点 DDS 全链路）

日期：2026-11-05

> 当前开发板未连接电脑，本文件只记录待执行的板端步骤，不代表板端已经验收。

当前离线状态：

- P1 数据通路联合仿真基线通过（DDS 1.5 MHz 主峰 bin 157）；P2 解耦控制链由独立/轻量集成仿真覆盖。
- 控制面/数据面已解耦：运行中改参数只写 PL staging + commit，由 PL 在帧边界生效；DMA 不停止、不重启。
- PL 独立测试 `tb_reg_config`、`tb_config_latch`、`tb_config_pipeline`、`tb_spectrum_frame_builder` 通过。
- C++/Qt 测试 20/20 通过。
- 312.5 MHz 宽带版本时序收敛：`WNS=+0.287 ns`，`WHS=+0.010 ns`，所有用户时序约束满足。
- 解耦版本 bitstream/XSA（含 312.5 MHz CIC 直通）已生成；板端部署尚未执行，因为开发板当前未连接。

## 1. 产物

- Bitstream：`DMA_2026_1012/DMA_TOP_20261106a.bit`
- XSA：`DMA_2026_1012/DMA_TOP_20261106a.xsa`
- 驱动源码：`driver/DMA/rtsa_dma_driver.c`
- UAPI：`driver/DMA/rtsa_dma_uapi.h`
- 设备树：`driver/DMA/system-user.dtsi`
- Qt 应用：板端 AArch64 `rtsa_app`（需要用目标 PetaLinux SDK 重新编译）

## 2. 上板前检查

板端需要同时满足：

```text
/sys/class/uio/uioX/name == rtsa_regs
/dev/rtsa_dma 存在
驱动槽位大小为 128 x 132 KiB
应用为当前源码编译出的 AArch64 版本
```

版本必须成套：

```text
bitstream  DMA_TOP_20261106a.bit
DTB        linux,uio-name = "rtsa_regs"
driver     当前 driver/DMA 源码编译的 rtsa_dma.ko
app        当前源码交叉编译的 AArch64 rtsa_app
```

设备树中 `&axi_bram_ctrl_0` 必须包含：

```dts
compatible = "generic-uio";
linux,uio-name = "rtsa_regs";
status = "okay";
```

## 3. 部署命令

先加载与本源码匹配的新 bitstream：

```bash
fpgautil -b DMA_TOP_20261106a.bit
```

重新加载新驱动：

```bash
rmmod rtsa_dma 2>/dev/null
insmod rtsa_dma.ko
ls -l /dev/rtsa_dma
```

确认 UIO 名称：

```bash
for f in /sys/class/uio/uio*/name; do echo "$f: $(cat "$f")"; done
```

启动 Qt：

```bash
env -u DISPLAY QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 ./rtsa_app --source dma
```

## 4. 界面参数

```text
信号来源       DDS
DDS 频率       1.5 MHz
CIC 抽取       8
CIC 截位       31
HB 级数        2
Max Hold       80 帧
FFT 点数       1024（固定）
采样率/Span    9.765625 MSPS
```

宽带 `312.5 MHz` 试验参数：

```text
信号来源       RFADC0 / RFADC1
CIC 抽取       1（旁路 CIC）
HB 级数        0
Max Hold       80 帧
FFT 点数       1024（固定）
Span           312.5 MHz
bin 间隔       305.18 kHz
近似 RBW       579.8 kHz（Blackman-Harris，ENBW≈1.90）
```

## 5. 运行时参数更新

采集过程中修改参数时，DMA 全程保持运行，PS 不执行 `RTSA_IOC_STOP` / `RTSA_IOC_START`，也不重新分配描述符环。所有参数按以下事务下发：

```text
UI 改参数
→ PS 写 staging 寄存器（0x100..0x224）
→ PS 写 0x120 COMMIT
→ PL 原子快照 staging
→ PL 等当前频谱帧完整输出
→ 锁存 active 配置
→ 软参数：下一帧换帧头
→ DDS 参数：额外产生一次 DDS resync
→ 硬参数：仅复位 PL 数据通路、重配 CIC、DDS resync
→ 新 epoch 帧继续写入同一个 DMA 环
```

- `center/span/trace/accum/window/calibration/max_hold`：软参数，帧边界生效，不复位数据通路。
- `DDS 频率`：帧边界生效，并产生一次 PL 内部 `dds_resync`。
- `DMA 模式、CIC 抽取/截位、HB 级数`：帧边界生效，PL 内部 `datapath_reset` 后自动重配 CIC，再产生 `dds_resync`。
- `CIC 抽取=1`：CIC 旁路，HB 仍可继续 `2^HB` 抽取；`CIC=1/HB=0` 时 FFT 输入为 `312.5 MSPS`，span 为 `312.5 MHz`。宽带直通使用 0% 重叠组帧，避免满速时帧控制器读不完一帧。
- `0x130 CONFIG_BUSY`：commit 事务进行中为 1，active 配置生效后自动清零。
- `0x134 ACTIVE_EPOCH`：PL 当前已生效的 `config_epoch`。
- 旧 epoch 帧会被 PS 丢弃；若 1 秒内未收到新 epoch，软件会告警并临时接受当前帧，避免界面永久黑屏。
- Linux 下 DMA 线程使用 `eventfd` 唤醒，参数修改不再受 20 ms `poll` 超时限制。

### 5.1 312.5 MHz 宽带试验

1. 设置 `CIC 抽取=1`、`HB 级数=0`、`Max Hold=80`。
2. 确认界面采样率/span 显示为 `312.5 MHz`。
3. 观察频谱：1024 点 FFT 的 bin 间隔约 `305.18 kHz`，不是窄带模式的 `9.54 kHz`。
4. 观察应用日志中的帧率和 DMA 接收率；宽带模式使用 0% 重叠，帧率低于窄带模式。
5. 检查 DMA 统计和 PL 的 `fft_fifo_overflow_sticky`/`frame_drop_count`；直通模式允许组帧按 0% 重叠运行，但不应出现 FFT 输入 FIFO overflow。

当前仿真基线：

```text
tb_dec_bypass:           CIC 旁路 valid 512/512；CIC=4 valid 125/512
tb_wideband_fft_smoke:   312.5 MSPS 连续输入，25.6 us 输出 2 帧，
                         FFT 输入 FIFO 无溢出
```

注意：`1024` 点 FFT 在 `312.5 MHz` span 下 RBW 约 `580 kHz`。如果需要更细的宽带分辨率，需要增加 FFT 点数或做信道化，不能靠界面缩放解决。

### 5.2 板端运行中配置验收

启动 DDS 采集并确认连续出帧后，依次执行：

1. 把 DDS 频率从 `1.5 MHz` 改为 `2.5 MHz`。
   - 界面主峰应立即移动到新频点附近。
   - 日志出现 `运行中配置已提交`，不应出现 `RTSA_IOC_STOP` 或 `RTSA_IOC_START`。
   - `0x134 ACTIVE_EPOCH` 递增，DMA 统计不应归零。
2. 把 CIC 从 `8` 改为 `16`，HB 保持 `2`。
   - 帧输出短暂停顿后恢复，span 变为 `4.8828125 MHz`。
   - `0x130 CONFIG_BUSY` 在事务期间为 1，随后自动清零。
   - 不应重新加载 bitstream，也不应重新 `insmod` 驱动。
3. 把信号来源从 `DDS` 切到 `RFADC0`，再切回 `DDS`。
   - 每次切换只复位 PL 数据通路，DMA 文件描述符和描述符环保持不变。
   - 中心频率在 RFADC 模式下锁定 `1.7 GHz`。
4. 连续快速修改多个参数。
   - 只允许最后一次 commit 生效，旧 epoch 帧被过滤。
   - 界面不黑屏、不撕裂，DMA 不报错。
5. 设置 `CIC=1`、`HB=0`，切到 312.5 MHz 宽带模式。
   - span 显示 `312.5 MHz`，bin 间隔约 `305.18 kHz`。
   - DMA 不停止，FFT 输入 FIFO 不溢出；帧率低于窄带模式属于预期。
   - 改回 `CIC=8/HB=2` 后恢复 9.765625 MHz。

板端若出现界面长时间不更新，先用应用内的 `PlRegisterMap` 读取 `0x130/0x134`，
并检查应用日志中是否出现 `等待 epoch=... 新帧超过 1 秒`。

## 6. 首帧判据

```text
magic            0x52545341（"RTSA"）
frame_bytes      2112
bin_count        1024
span_hz          9765625
data_format      0（int16 对数幅度，0.01 dB/LSB）
主峰频点         约 bin 157
```

应用日志应出现 `[DMA] 首帧捕获成功`，并报告 `peak_bin=157` 左右。

## 7. RFADC 实采验证

当前 bitstream 中 ADC0/ADC1 的 RFDC 配置为：

```text
采样率          5 GSPS
DDC 抽取        4
RFDC 输出       1.25 GSPS（4 样点 × 312.5 MHz）
固定 NCO        1.7 GHz
PL 多相合成后   312.5 MSPS
```

验证步骤：

1. 给 ADC0 或 ADC1 输入约 `1.7015 GHz` 的单音，幅度低于 ADC 满量程。
2. 界面“信号来源”选择 `RFADC0` 或 `RFADC1`。
3. 中心频率会自动锁定到 `1.7 GHz`；CIC=8、HB=2 时 span 为 9.765625 MHz。
4. 预期主峰在 bin 157 附近；若输入正好为 1.7 GHz，则主峰在 DC 附近。
5. 如果无峰，依次检查 RFDC `tvalid`、SYSREF、ADC 时钟、输入电平和 over-range。

## 8. 当前待完成

1. 等待开发板连接后执行以上步骤。
2. 用 AArch64 工具链重新编译当前 Qt 应用和 `rtsa_dma.ko`。
3. 记录首帧、连续采集、运行中修改软/硬参数（DMA 不停）和长时间运行结果。
