# 专题 01：无 GPU 依赖的 CPU Raster 异步离屏渲染

## 1. 文档范围

本文描述 `SpectrumPlotWidget` 和 `RasterSpectrumRenderer` 当前如何降低主线程软光栅负担。项目显示路径不依赖 OpenGL、Qt Quick 或 GPU 驱动，但本文不把“无 GPU 渲染”扩展成未经实测的板端帧率结论。

| 项目 | 状态 |
|---|---|
| CPU Raster、静态层、单离屏线程、脏区贴图 | 已实现 |
| Windows Qt 6 历史性能记录 | PC 历史验证，本次未复测 |
| ZU47DR 目标 QPA 下的帧率、主线程耗时、显示延迟 | 板端待验 |

## 2. 当前数据与绘制路径

```text
采集线程
  -> SpectrumPipeline::submit()
  -> LatestFrameStore
  -> MainWindow 10 ms 显示定时器
  -> SpectrumPlotWidget::setFrame()
  -> 单个 renderWorker_
  -> RasterSpectrumRenderer::renderToImage()
  -> Format_RGB16 frontImage_
  -> paintEvent() 只贴事件指定的脏区
```

关键实现位于：

- `src/plot/SpectrumPlotWidget.cpp`
- `src/plot/SpectrumPlotWidget.h`
- `src/plot/RasterSpectrumRenderer.cpp`
- `src/plot/RasterSpectrumRenderer.h`
- `src/core/EnvelopeReducer.cpp`

## 3. 已实现机制

### 3.1 单个离屏工作线程

`SpectrumPlotWidget` 构造时启动一个 `std::thread`。`setFrame()` 生成 `RenderContext`，在互斥量保护下写入 `pendingContext_`，然后唤醒工作线程。

这里得到的是“最新上下文覆盖”语义：

- 运行中不建立无限增长的渲染队列；
- 新帧到来时，后到的上下文可以覆盖尚未开始的旧上下文；
- 工作线程完成当前图像后，主线程通过队列连接收到一次脏区更新；
- 源码没有设置 CPU 亲和性，也没有创建 CPU0/CPU1/CPU2 线程池。

因此，文档不应表述为“四核并行离屏渲染”或“CPU0 至 CPU3 自动负载均衡”。

### 3.2 静态层与目标图

`RasterSpectrumRenderer` 将背景、网格、坐标和标签绘制到 `staticLayer_`。静态层和目标图都使用 `QImage::Format_RGB16`。每帧渲染时：

1. 静态层几何变化时重建静态层；
2. 复用或复制同格式目标图；
3. 在目标图上绘制轨迹和 Marker；
4. 通过 `frontImage_` 发布完整图像。

这减少了 32 位中间图和 16 位目标图之间的格式混用，但最终 LinuxFB 合成路径仍由 Qt QPA、窗口 backing store 和 Framebuffer 格式决定，不能只凭源码断言已经实现从应用图像到 `/dev/fb0` 的严格 1:1 拷贝。

### 3.3 脏区贴图与同步兜底

`paintEvent()` 读取事件矩形，有可用离屏图时只执行：

```cpp
painter.drawImage(dirty.topLeft(), frontImage_, dirty);
```

离屏图尚未就绪、尺寸不匹配或主动失效时，退回当前线程同步调用 Raster 绘制。该兜底保证冷启动和测试路径仍可显示，但也意味着“所有帧永远异步完成”的表述不成立。

## 4. 频谱几何的实际处理

`EnvelopeReducer::reduce()` 按绘图区宽度把完整频点压缩为若干列，每列保留 minimum 和 maximum。当前绘制路径只使用 `maximum` 组成一条折线：

```cpp
const int topY = yForAmplitude(value.maximum);
peakPolyline_.append(QPoint(x, topY));
```

源码中的 `envelopeLines_` 已被清空，也没有真正绘制。当前没有把折线强制限制为 512 点，而是以 `plotRect_.width()` 为目标列数。最终点数还受频点数和绘图区宽度影响。

这种列映射的价值是：窄带峰落入某列时仍可通过该列最大值显示，而不是被简单跨 bin 抽点丢掉。它不是“只绘制 512 点”或“不需要任何包络计算”的实现。

## 5. 性能证据与边界

2026-07-26 的 Windows Qt 6 历史记录包含 16,384 点、请求 1,000 FPS、1920×1080 主窗口等条件；历史 Release 记录的显示循环约为 33.70 FPS，P95 显示延迟约为 16.5 ms。该结果说明 PC 离屏路径曾经可运行，但不能证明 ZU47DR 达到 60 FPS。

仓库中曾出现“37 ms 降至 2 至 3 ms”“稳定 60 FPS”“四核负载均衡”等数值，但当前文档没有找到与最终源码、目标板、原始日志和重复实验一一对应的证据，因此不作为当前结论保留。

板端至少需要记录：

| 指标 | 采集方式 |
|---|---|
| `paintEvent()` 耗时 | 控件现有计时或外部 instrumentation |
| 离屏任务间隔 | 帧到达序号和完成序号 |
| 主线程阻塞时间 | 同步兜底次数、事件循环延迟 |
| 显示延迟 | `framePainted` 的发布时间戳与绘制时间 |
| CPU 占用 | 按线程采样，不使用整机平均代替单核分析 |
| QPA 实际格式 | Qt 日志、`fb_var_screeninfo` 和必要的系统调用跟踪 |

## 6. 验证重点

- 1920×1080 与目标板真实分辨率；
- 1,024、16,384、65,536 点输入；
- 1 FPS、目标显示帧率和过载输入；
- 连续缩放、拖动、全屏切换和主题切换；
- 离屏工作线程停止时能否及时退出；
- 数据帧跨线程发布是否只通过不可变快照；
- 同步兜底是否只在冷启动或异常路径发生。

## 7. 相关文档

- [总体实现架构与设计方案](../../design/实时频谱分析仪软件总体实现架构与设计方案.md)
- [渲染优化资料索引](../render_optimization/README.md)
- [项目实现与验证报告](../项目实现与验证报告.md)
