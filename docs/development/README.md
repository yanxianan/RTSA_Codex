# 开发文档

本目录保存构建、测试、驱动、板端联调和实现状态。

以下命令和路径均默认从**仓库根目录**执行。

## 快速入口

- [P2 板端联调清单](P2_BOARD_BRINGUP.md)
- [DMA 驱动解析与板端部署](DMA驱动解析.md)
- [2026-11-16 软件问题修复记录](2026-11-16_软件问题修复记录.md)
- [2026-11-08 板端联调交接记录](2026-11-08_板端联调交接记录.md)
- [项目实现与验证报告](项目实现与验证报告.md)
- [需求追踪矩阵](需求追踪矩阵.md)
- [上位机功能介绍与使用说明](实时频谱分析仪上位机功能介绍与使用说明.md)
- [核心实现专题](innovations/README.md)
- [渲染性能专题](render_optimization/README.md)

## 当前上板前置条件

1. 加载 `DMA_2026_1012/DMA_TOP_20261106a.bit`，然后重新加载 `rtsa_dma.ko`（PL 重配置会清掉 AXI DMA 的 IRQ 使能位）。
2. 设备树 DTB 包含：

```dts
&axi_bram_ctrl_0 {
    compatible = "generic-uio";
    linux,uio-name = "rtsa_regs";
    status = "okay";
};
```

3. 用当前 `driver/DMA` 源码重新编译 `rtsa_dma.ko`：

```bash
cd driver/DMA
make KDIR=/path/to/linux-xlnx ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

4. 用当前源码交叉编译 AArch64 Qt 应用。
5. 启动后确认：

```bash
ls -l /dev/rtsa_dma
for f in /sys/class/uio/uio*/name; do echo "$f: $(cat "$f")"; done
```

## 当前验证基线

- C++/Qt：20/20 CTest。
- PL 控制链：`tb_reg_config`、`tb_config_latch`、`tb_config_pipeline`。
- 宽带：`tb_dec_bypass`、`tb_wideband_fft_smoke`。
- 时序：`WNS=+0.287 ns`，`WHS=+0.010 ns`。
- 板端已连接；中文显示已修复，DMA 首帧链路仍在排查，不能把主机测试当成板端验收。
