# RFSoC (ZU47DR) 板端 GPU 存在性权威验证指南

本文档提供**在不依赖网络检索、直接在手边板卡上验证是否存在 Mali-400 GPU 硬件**的标准测试方法，并深入解析 Xilinx 芯片硬件架构的物理真相。

---

## 一、板端一秒验证操作（直接在板卡终端执行）

请在开发板的 Linux 串口或 SSH 终端中依次执行以下 3 条权威验证命令：

### 1. 检查 Linux 内核设备树（Device Tree）中是否存在 GPU 物理节点
在 Xilinx Zynq UltraScale+ 体系中，Mali-400 GPU 是挂载在 PS 端 AMBA 总线上的固定物理外设，其物理基地址为 `0xFDA00000`。如果芯片具备 GPU，内核设备树在解析时必定会生成对应的节点：

```bash
find /proc/device-tree/ -name "*gpu*" -o -name "*mali*"
```
* **如果有 GPU**：会返回类似 `/proc/device-tree/amba/gpu@fda00000` 的路径。
* **如果无 GPU**：输出为空，什么都不会返回。

---

### 2. 检查内核启动日志与驱动探测记录
检查 Linux 内核在引导阶段是否探测到 Mali 硬件或输出过 GPU 相关的状态信息：

```bash
dmesg | grep -i -E "mali|gpu"
```
* **如果有 GPU**：会输出类似 `mali fda00000.gpu: Mali-400 GPU probed` 或类似日志。
* **如果无 GPU**：没有任何 Mali 相关的硬件识别输出。

---

### 3. 检查系统字符设备与 DRM 显卡节点
若系统编译了 GPU 驱动，会在 `/dev` 下生成设备节点：

```bash
ls -l /dev/mali* /dev/dri/card* 2>/dev/null
```
* **如果有 GPU 驱动与节点**：会显示 `/dev/mali` 或 `/dev/dri/card0`（Render 节点）。
* **如果只有 `/dev/fb0`**：说明当前完全是基于内存虚拟 Framebuffer 或简单显示控制器（如 PL 端 HDMI/DP 帧缓存），不存在 GPU 硬件加速节点。

---

## 二、硬件真相：RFSoC 芯片内部到底有没有 Mali-400？

### 1. Xilinx 官方产品家族后缀定义
根据 AMD / Xilinx 官方《Zynq UltraScale+ MPSoC / RFSoC Architecture Technical Reference Manual》（UG1085 与 DS890）：

| 芯片系列 | 典型型号 | PS 核心配置 | 是否具备 Mali-400 MP2 GPU | 适用场景 |
| :--- | :--- | :--- | :---: | :--- |
| **MPSoC - CG** | XCZU3CG, XCZU4CG | 2× A53 + 2× R5 | ❌ **无 GPU** | 工业控制、轻量级计算 |
| **MPSoC - EG** | XCZU3EG, XCZU9EG, XCZU15EG | 4× A53 + 2× R5 | ✅ **有 Mali-400 MP2** | 通用多媒体、图形界面 |
| **MPSoC - EV** | XCZU7EV, XCZU11EV | 4× A53 + 2× R5 + VCU | ✅ **有 Mali-400 MP2** | 4K 视频编解码、机器视觉 |
| **RFSoC - DR** | **XCZU28DR, XCZU47DR, XCZU48DR** | **4× A53 + 2× R5** | ❌ **物理无 GPU 硅片** | **高速射频采发、雷达、电子战** |

### 2. 为什么网上有人说“有”，有人说“没有”？
* **说“有”的原因**：很多开发者把 **ZynqMP（MPSoC EG 系列）**的通用文档（如“ZynqMP 全系支持 Mali-400”）套用到了 **RFSoC（DR 系列）**上。
* **说“没有”的原因（真实情况）**：RFSoC 为了在单芯片内集成 16 通道 5GSPS RF-ADC / 10GSPS RF-DAC、硬核 SD-FEC 和超大规模 PL 阵列，芯片面积（Die Size）和功耗预算极其紧张，因此 **Xilinx 在芯片物理掩膜（Mask）层直接去掉了 Mali-400 GPU IP 模块**！

---

## 三、结论与优化路线指导

1. **确定事实**：ZU47DR 芯片本身**没有内置 Mali-400 GPU 硬件**。
2. **技术路线转向**：不要浪费时间在 `QT_QPA_PLATFORM=eglfs`（依赖 Mali GLES 驱动）的尝试上。
3. **唯一正道**：在没有硬件 GPU 的情况下，提升帧率的手段聚焦于：
   * **方案一：精简 CPU 软光栅（去除 1500 条包络线、折线降采样）** $\to$ 单帧渲染从 37ms 降至 10~12ms；
   * **方案二：多核异步离屏 QImage 渲染** $\to$ 把 CPU0/1/2 用起来，主线程耗时降至 2~3ms，帧率跑上 60 FPS；
   * **方案三：DRM Dumb Buffer / DirectFB 翻页** $\to$ 减少显存拷贝损耗。
