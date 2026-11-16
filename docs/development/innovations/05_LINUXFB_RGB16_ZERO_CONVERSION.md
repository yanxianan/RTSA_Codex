# 专题 05：RGB16 静态层、脏区贴图与主线程几何解耦

## 1. 文档范围

本文说明频谱控件当前的 16 位图像路径和离屏几何计算。旧标题中的“零转换”容易让人理解为 Qt 到 LinuxFB 的整个链路绝不会做格式转换，这一结论目前没有板端证据，因此本文改用可验证的实现描述。

| 项目 | 状态 |
|---|---|
| 静态层和离屏目标图使用 `Format_RGB16` | 已实现 |
| 单个离屏线程构建几何并绘制轨迹/Marker | 已实现 |
| 主线程通过脏区 `drawImage()` 显示结果 | 已实现 |
| LinuxFB 最终合成是否直接使用 RGB16、实际拷贝次数和耗时 | 板端待验 |

## 2. 相关源码

- `src/plot/RasterSpectrumRenderer.cpp`
- `src/plot/RasterSpectrumRenderer.h`
- `src/plot/SpectrumPlotWidget.cpp`
- `src/plot/SpectrumPlotWidget.h`

## 3. 已实现图像路径

```text
RenderContext
  -> offscreenRenderer_.renderToImage()
  -> staticLayer_ / targetImage: Format_RGB16
  -> drawTraceAndMarkers()
  -> frontImage_
  -> paintEvent(): drawImage(dirty, frontImage_, dirty)
```

### 3.1 静态层

`RasterSpectrumRenderer::rebuildStaticLayer()` 创建 `QImage::Format_RGB16`，绘制背景、网格、坐标、单位、中心频率和 Span。静态层只在尺寸、主题、坐标或样式变化时重建。

### 3.2 离屏目标图

`renderToImage()` 确保目标图与静态层尺寸、格式一致。复用同一缓冲区时使用 `memcpy` 复制静态层，再绘制实时轨迹和 Marker。

源码可以证明应用内部使用了 RGB16，但不能单独证明：

- Qt Widget backing store 一定是 RGB16；
- linuxfb 插件没有进行最终像素转换；
- `drawImage()` 等于对 Framebuffer 的逐行 1:1 拷贝；
- 任意板卡、分辨率和 QPA 下都能获得相同收益。

这些结论需要在目标 rootfs 上检查 Qt 平台插件、Framebuffer 格式和系统调用/性能数据。

## 4. 主线程与离屏线程职责

`SpectrumPlotWidget::setFrame()` 在主线程中更新当前帧指针、同步 Marker，并构造 `RenderContext`。几何构建和图像绘制由单个 `renderWorker_` 执行。

主线程的常规显示工作包括：

- 读取已经完成的 `frontImage_`；
- 只在事件指定脏区绘制图像；
- 处理输入、窗口和控件事件。

冷启动、图像尺寸不匹配或主动失效时，`paintEvent()` 会同步调用 `renderer_.paint()`。因此主线程并非在所有情况下都不执行几何或光栅化。

## 5. 为什么使用单工作线程

该设计的优先目标是：

1. 不让主事件循环持续执行完整轨迹绘制；
2. 只保留最新待渲染上下文，减少旧帧排队；
3. 避免多个渲染线程同时争用内存带宽；
4. 在离屏结果无效时可退回同步渲染。

源码没有绑定 CPU 核，也没有把一帧拆成多个并行任务。实际调度由操作系统决定。

## 6. 性能证据

早期板端记录曾给出 `paintEvent()`、32 位到 16 位转换、P95 延迟和 60 FPS 等数字，但当前资料没有保留与这些数字对应的完整源码版本、启动参数、采样命令和原始日志。本文因此只保留为待复核线索，不作为当前性能结论。

可复核的 PC 历史数据来自 2026-07-26 的 Windows Qt 6 测试，条件是 16,384 点、请求 1,000 FPS 和 1920×1080 主窗口。历史 Release 记录的输入约 1,000 FPS、显示约 33.70 FPS、P95 延迟约 16.5 ms。它不能代表 ZU47DR 板端。

## 7. 板端验证方法

| 检查项 | 方法 |
|---|---|
| Qt 平台与屏幕参数 | `QT_LOGGING_RULES="qt.qpa.*=true"`、`fbset` 或 `fb_var_screeninfo` |
| 图像格式 | 在目标环境记录 `staticLayer_.format()` 和 `frontImage_.format()` |
| 合成路径 | 结合 Qt QPA 日志、必要的 `strace` 和绘制计时 |
| 主线程耗时 | 区分异步贴图帧和同步兜底帧 |
| 脏区行为 | 记录 `QPaintEvent::rect()` 与更新区域 |
| 端到端延迟 | 发布时间戳、`framePainted` 时间和显示采样 |

## 8. 验收边界

- RGB16 路径属于已实现优化，不是已完成板端性能验收；
- “静态层格式与目标图格式一致”不能替代 FrameBuffer 链路实测；
- 60 FPS 仅是可能的运行目标或历史观察，不能写成当前稳定结果；
- 任何性能收益都应附带硬件、分辨率、Qt 版本、QPA、场景和原始统计。

## 9. 相关文档

- [专题 01：CPU Raster 异步离屏渲染](./01_EMBEDDED_RENDERING_OPTIMIZATION.md)
- [GPU/显示加速核验指南](../render_optimization/GPU_VERIFICATION_GUIDE.md)
- [渲染优化方案与验证计划](../render_optimization/RENDER_OPTIMIZATION_PLAN.md)
- [项目实现与验证报告](../项目实现与验证报告.md)
