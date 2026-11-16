# RTSA 文档中心

更新日期：2026-11-06

文档按用途分为三类。**当前代码和寄存器实现是最终事实来源**；文档与代码冲突时，先以代码为准并回头修正文档。

## 当前实现快照

- PL：1024 点复数 FFT，DDS/RFADC0/RFADC1 可选，CIC + HB 抽取。
- 宽带：`CIC=1` 为 CIC 旁路，`CIC=1/HB=0` 时 FFT 输入 312.5 MSPS、span 312.5 MHz。
- 控制面：PS staging + `0x120 COMMIT`，PL 帧边界锁存；DMA 运行中不停止、不重启。
- 状态：`0x130 CONFIG_BUSY`、`0x134 ACTIVE_EPOCH`。
- DMA：128 × 132 KiB 槽位，`frame_size` 可配置，支持 `RTSA_IOC_SET_FRAME_SIZE` / `RTSA_IOC_GET_STATS`。
- 新 bitstream：`DMA_2026_1012/DMA_TOP_20261106a.bit`；XSA 同名 `.xsa`。
- 时序：`WNS=+0.287 ns`，`WHS=+0.010 ns`。
- 板端：尚未连接验证。

## 1. 设计方案

面向“系统应该怎么设计、接口如何约定、指标如何计算”的文档：

- [软件需求规格说明书](design/实时频谱分析仪软件需求规格说明书.md)
- [软件总体实现架构与设计方案](design/实时频谱分析仪软件总体实现架构与设计方案.md)
- [PL/PS 频谱接口设计](design/PL_PS_Spectrum_Interface_Design_v1.md)
- [DMA 数据源接口与 PL/PS 协议](design/DMA数据源接口与PL_PS协议确认清单.md)
- [ZU47DR PL/AXI DMA 接入差距与实施方案](design/ZU47DR_PL_AXIDMA频谱数据接入差距分析与后续实施方案.md)

设计文档中的“未来/待实现”章节可能包含规划内容，不应直接当作已实现功能。

## 2. 开发文档

面向“怎么构建、怎么部署、怎么测试、当前完成到什么程度”的文档：

- [P2 板端联调清单](development/P2_BOARD_BRINGUP.md)
- [DMA 驱动解析与板端部署](development/DMA驱动解析.md)
- [项目实现与验证报告](development/项目实现与验证报告.md)
- [需求追踪矩阵](development/需求追踪矩阵.md)
- [上位机功能介绍与使用说明](development/实时频谱分析仪上位机功能介绍与使用说明.md)
- [核心实现专题索引](development/innovations/README.md)
- [渲染性能资料索引](development/render_optimization/README.md)

开发文档优先反映当前代码；`P2_BOARD_BRINGUP.md` 是上板操作的权威入口。

## 3. 学习资料

面向“理解原理、回顾设计过程、查看历史对话”的材料：

- [其他材料](learning/其他材料/)：DMA、内核、数据流、渲染、毕业设计等专题讲解
- [智能体对话记录](learning/智能体对话记录/README.md)：历史开发会话与原始过程记录
- [优化记录](learning/优化记录/)：UI、渲染、无 GPU 显示等优化过程

学习资料保留历史原貌，**可能引用旧寄存器、旧驱动或旧带宽参数**；学习时请对照当前设计文档和代码。

## 维护约定

- 新增系统级设计：放 `docs/design/`。
- 新增构建、测试、板端、驱动说明：放 `docs/development/`。
- 新增原理讲解、历史记录、优化过程：放 `docs/learning/`。
- 文档中的寄存器地址、槽位大小、带宽公式必须与 `src/core/PlRegisterMap.h`、`driver/DMA/rtsa_dma_uapi.h` 和当前 bitstream 一致。
