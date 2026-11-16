# 阶段 A 无 GPU 渲染优化实施记录

## 1. 实施范围

本次实施对应《无 GPU 嵌入式渲染与显示优化分析》中的阶段 A：

- S0：分段计时、百分位统计、覆盖计数和合成基准模式；
- S1：离屏渲染 Ping-Pong 双缓冲；
- S2：每帧只恢复绘图区静态背景；
- S3：瀑布图静态层和色条缓存；
- S5：`direct / offscreen` 渲染模式；
- S6：30/60/100 FPS 显示刷新上限。

本阶段不包含 S4（预量化 RGB16 瀑布历史缓存），也不改变频谱数据、峰值搜索、
Marker 坐标或 bin 到频率的映射。

## 2. 频谱图渲染优化

### 2.1 静态层缓存

`RasterSpectrumRenderer` 现在缓存与控件同尺寸的 RGB16 静态层，内容包括：

- 窗口和绘图区背景；
- 网格、边框；
- 频率/幅度坐标文字；
- 中心频率、Span 和单位文字。

首次渲染、控件尺寸变化、频率范围变化、幅度范围变化、主题变化或文字变化时，
静态层才重建。普通帧只把 `plotRect_` 区域从静态层恢复到目标图，再绘制轨迹和
Marker，避免每帧复制整幅图像。工作线程发布新图时会比较静态层代数；坐标轴或
文字对应的静态层发生变化时改为整窗重绘，防止绘图区外的标签短暂残留旧内容。

### 2.2 Ping-Pong 双缓冲

离屏线程持有本地工作图，发布时使用 `QImage::swap()` 与前台图交换。交换后：

- 前台图持有刚完成的新帧；
- 工作线程持有上一帧图像，可安全复用；
- 避免 `frontImage_ = localBackBuffer` 引发的 QImage 写时复制和额外分配。

### 2.3 帧归属修正

前台图发布时同时保存实际渲染帧的 `publicationSequence` 和 `timestampNs`。
`framePainted` 现在对应真正被绘制的帧，而不是 GUI 当时看到的最新输入帧。

## 3. 瀑布图渲染优化

瀑布图新增完整静态层，缓存：

- 背景和绘图区背景；
- 频率轴、时间轴文字；
- 色标边框、文字和 `1x256` 色条图。

数据更新时只使动态历史区失效。`paintEvent()` 按事件脏矩形绘制：

1. 静态层；
2. 动态瀑布历史；
3. 网格和边框覆盖层。

同时修复了历史深度变化时缓冲区没有真正重分配的问题，并补充 `addFrame()` /
`paintEvent()` 耗时统计。

## 4. 渲染路径与刷新调度

新增可切换模式：

- `offscreen`：默认模式，后台线程渲染，GUI 线程贴图；
- `direct`：GUI 线程直接调用 `RasterSpectrumRenderer::paint()`。

新增 30/60/100 FPS 三档显示上限。默认仍为 100 FPS，不改变既有默认行为。
工作线程继续使用“只保留最新待渲染上下文”的覆盖策略，不建立无限队列。

## 5. 诊断与合成基准

新增固定 256 样本滚动计时统计，输出 last/P50/P95/P99：

- 几何构建；
- 静态层构建；
- 静态背景恢复；
- 轨迹和 Marker；
- 离屏总渲染；
- GUI `paintEvent()`；
- 瀑布图数据写入和绘制。

另外记录：

- 待渲染上下文覆盖次数；
- 同步兜底次数；
- Direct/Offscreen 绘制次数；
- 实际绘制帧率；
- Qt 绘制完成延迟。

新增合成基准模式：

```text
--render-benchmark static|trace|waterfall|mixed
```

- `static`：只生成一次完整频谱图；Offscreen 模式下重复显示缓存图，用于测量
  GUI 贴图、backing store 和 QPA 提交下限，Direct 模式下则重复执行静态直绘；
- `trace`：持续生成动态频谱轨迹，只显示频谱图；
- `waterfall`：只显示瀑布图；
- `mixed`：同时显示频谱图和瀑布图。

通过命令行启动基准模式时不会启动真实数据源，也不会把基准视图设置写入用户配置。

## 6. Qt 5.15 兼容性

本阶段使用的 API 均不高于 Qt 5.15：

- `QThread::create()`：Qt 5.10+；
- 带上下文对象的 functor 版 `QMetaObject::invokeMethod()`：Qt 5.10+；
- `QImage::swap()`：Qt 5；
- `QFontMetrics::horizontalAdvance()`：Qt 5.11+；
- Qt 6 的 `QMouseEvent::position()` 和 `QWheelEvent::position()` 均有
  `QT_VERSION` 条件分支，Qt 5 下使用 `pos()`。

没有引入 Qt 6 独有的事件、坐标、容器或元对象 API。

## 7. 验证结果

开发机环境：

- Windows 11；
- Qt 6.10.2 MinGW 64-bit；
- Debug 构建。

验证结果：

- 全量构建成功；
- CTest 19/19 通过；
- 新增 Direct 渲染烟测通过；
- 新增 static 和 mixed 合成基准烟测通过；
- 非法基准模式校验测试通过；
- 未手动注入 Qt PATH 时，3 个渲染烟测仍通过，说明 CTest 运行环境配置完整。

旧颜色断言已修正为按 RGB565 量化值比较。静态层固定使用
`QImage::Format_RGB16`，例如浅色背景 `#e8ecf0` 实际输出为 `#efeff7`，属于
预期格式量化，不是渲染错误；轨迹随后由 QPainter 绘制到 ARGB 目标图，仍保持
原始主题颜色。

## 8. 板端验证步骤

开发机验证不能替代目标板验证。建议在 ZU47DR/PetaLinux/Qt 5.15 上执行：

```bash
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0

./rtsa_app --source simulated --render-mode offscreen \
  --display-fps 30 --render-benchmark static \
  --render-diagnostics --no-file-log

./rtsa_app --source simulated --render-mode offscreen \
  --display-fps 30 --render-benchmark trace \
  --render-diagnostics --no-file-log

./rtsa_app --source simulated --render-mode offscreen \
  --display-fps 30 --render-benchmark mixed \
  --render-diagnostics --no-file-log
```

板端矩阵至少覆盖：

- 2560x1440 和 1920x1080；
- 30/60/100 FPS；
- Direct 和 Offscreen；
- 仅频谱、仅瀑布图、双视图；
- 1024、16384、65536 点；
- 残影、撕裂、同步兜底次数、P95 耗时和各 CPU 核占用。

只有完成上述板端对照并留存日志，才能给出实际帧率提升比例和最终默认模式。
