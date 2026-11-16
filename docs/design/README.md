# 设计方案

本目录保存系统级设计、接口契约和带宽/资源规划。

## 当前设计结论

```text
ADC 5 GSPS
→ RFDC DDC 抽取 4
→ 1.25 GSPS 复数
→ PL 多相 4:1
→ 312.5 MSPS 复数基带
→ CIC 抽取 R（R=1 时旁路）
→ HB 抽取 2^L
→ Fs = 312.5 / (R × 2^L) MSPS
→ 1024 点复数 FFT
```

- 实时 span = `Fs`（复数 FFT）。
- bin 间隔 = `Fs / 1024`。
- RBW ≈ bin 间隔 × 窗函数 ENBW。
- `CIC=1/HB=0`：span 312.5 MHz；`CIC=8/HB=2`：span 9.765625 MHz。
- `CIC=1` 是 PL 旁路模式，不是正常的 1 倍 CIC 抽取。

## 文档

- [软件需求规格说明书](实时频谱分析仪软件需求规格说明书.md)
- [软件总体实现架构与设计方案](实时频谱分析仪软件总体实现架构与设计方案.md)
- [PL/PS 频谱接口设计](PL_PS_Spectrum_Interface_Design_v1.md)
- [DMA 数据源接口与 PL/PS 协议](DMA数据源接口与PL_PS协议确认清单.md)
- [ZU47DR PL/AXI DMA 接入差距与实施方案](ZU47DR_PL_AXIDMA频谱数据接入差距分析与后续实施方案.md)

> 设计文档中的“后续/建议/P3”章节是规划，不代表已经实现；当前实现状态以 [开发文档](../development/) 和代码为准。
