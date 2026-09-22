# RTSA 渲染性能与帧率优化专栏 (render_optimization)

本目录专门用于彻底解决 RTSA 频谱分析仪在嵌入式 LinuxFB（无 GPU 硬件加速）环境下**渲染速度慢、界面刷新帧率低（当前仅 15~25 FPS）**的问题。

---

## 文档索引

1. [**GPU_VERIFICATION_GUIDE.md**](./GPU_VERIFICATION_GUIDE.md)  
   **《RFSoC (ZU47DR) 板端 GPU 存在性权威验证指南》**
   * 提供 3 条直接在开发板终端敲击的物理验证命令（查设备树、查内核日志、查设备节点）；
   * 深度解析 Xilinx UltraScale+ 芯片命名规范（EG/EV 与 DR 的本质差异），给出芯片级物理真相。

2. [**RENDER_OPTIMIZATION_PLAN.md**](./RENDER_OPTIMIZATION_PLAN.md)  
   **《RTSA 渲染速度慢与显示低帧率深度优化方案》**
   * **性能根因拆解**：为何单核打满、37ms 耗时花在哪里；
   * **方案一（精简 CPU 软光栅）**：砍掉 1500 条垂直线、折线降采样至 512 点、局部脏矩形刷新（**改动小，耗时直降至 10~12ms，帧率翻倍至 50+ FPS**）；
   * **方案二（多核异步离屏 QImage 渲染）**：压榨 CPU0/1/2，后台线程绘制 QImage，主线程只做贴图（**耗时降至 2~3ms，帧率稳跑 60 FPS**）；
   * **方案三（底层显示管道与替代加速）**：基于无 Mali GPU 事实，分析 DRM Dumb Buffer 及架构级解耦方案；
   * **具体实施代码与推进路线图**。
