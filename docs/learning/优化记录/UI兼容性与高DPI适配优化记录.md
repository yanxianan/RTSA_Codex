# UI 兼容性与高 DPI 适配优化记录

| 项目 | 内容 |
|---|---|
| 记录日期 | 2026-10-26 |
| 优化主题 | 开发板 2K 显示器 UI 兼容性、窗口边界与高 DPI 自适应 |
| 涉及模块 | 主窗口布局、参数控制栏、表单滚动区、频率输入控件、Qt 高 DPI 初始化 |
| 代码状态 | 已完成实现并通过本机自动化验证 |
| 板端状态 | 尚未在 ZU47DR 实际系统、实际 QPA 和实际 2K framebuffer 上复测 |
| 相关文档 | `其他材料/UI兼容性.md` |

## 1. 优化背景

项目在 Windows 开发机上运行时，界面布局显示正常；部署到 ZU47DR 开发板并连接 2K 显示器后，出现了与显示环境相关的 UI 兼容性问题。

从现场截图可以观察到以下现象：

- 左侧频谱绘图区被压缩成窄条，只保留很小一部分可见空间；
- 右侧参数控制栏占据了大部分窗口宽度；
- 分组标题、表单标签和部分控件的左侧内容出现裁切；
- 采集状态、频率输入框、FFT 点数和瀑布图色标仍然存在，说明程序主体已经启动，频谱数据链路和绘制模块并未完全失效；
- 界面没有出现整体旋转、花屏或 OpenGL 着色错误，问题更符合窗口尺寸、布局分配和高 DPI 缩放异常。

该问题不能通过“本机可以正常运行”来排除。Windows 环境中的窗口管理器、字体度量、Qt 版本、QPA 后端和缩放策略共同满足了原有固定布局的隐含假设；开发板环境中的这些条件发生变化后，原本不可见的布局缺陷就被暴露出来。

## 2. 优化前存在的问题

### 2.1 启动窗口使用固定尺寸

优化前主窗口直接执行：

```cpp
resize(1440, 860);
```

该尺寸没有参考当前屏幕的 `availableGeometry()`。当开发板的逻辑分辨率、缩放比例或可用区域小于该尺寸时，窗口可能超出屏幕边界。

在存在窗口管理器的桌面环境中，窗口管理器通常会将越界窗口拉回可见区域；但在 `linuxfb` 等没有完整窗口管理器的嵌入式 QPA 上，超出的部分可能直接位于 framebuffer 之外，表现为左侧或底部内容不可见。

### 2.2 参数栏固定为 340 px

优化前右侧参数栏使用：

```cpp
panel->setFixedWidth(340);
```

固定宽度带来了两个问题：

1. 参数栏不能随字体、DPI 和窗口宽度调整；
2. 主区域始终采用左右并排布局时，参数栏会优先占据空间，导致频谱绘图区被持续压缩。

频谱绘图区是该软件的主要信息载体，参数栏属于辅助操作区。原有布局没有明确这一优先级，因此出现了“参数区可见、频谱区只剩窄条”的现象。

### 2.3 主界面没有响应式断点

优化前主区域只有固定的横向布局：

```text
左侧：频谱 + 瀑布图
右侧：参数控制栏
```

代码没有根据窗口宽度、宽高比或屏幕方向切换布局。对于 2K 屏高比例缩放、竖屏显示或逻辑宽度较小的场景，横向布局仍然会被强制执行。

### 2.4 表单横向溢出被直接隐藏

优化前多个参数页使用 `QScrollArea`，但将水平滚动条永久关闭：

```cpp
setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
```

隐藏滚动条并不会改变表单内容的最小宽度。当字体变大、DPI 提高或下拉框变宽时，`QFormLayout` 的内容仍可能超过视口宽度。超出部分会被裁切，用户无法通过滚动访问。

这会直接导致表单标签、输入框左侧内容或单位控件不可见。

### 2.5 表单没有启用换行策略

优化前 `QFormLayout` 依赖默认布局行为，没有显式设置：

- `WrapLongRows`；
- `WrapAllRows`；
- `AllNonFixedFieldsGrow`；
- 顶部左对齐策略。

在参数栏宽度较窄时，长标签或较长控件不会稳定换到下一行，容易超出可用宽度。

### 2.6 频率单位下拉框宽度固定

优化前频率单位下拉框固定为 `64 px`：

```cpp
unitCombo_->setFixedWidth(64);
```

不同字体、语言和 DPI 下，`MHz`、`GHz` 等文字所需宽度并不相同。固定宽度可能造成文字显示不全，也会进一步压缩频率数值输入框。

### 2.7 恢复旧窗口几何时没有检查可见性

程序会通过 `QSettings` 保存窗口几何，并在下次启动时调用 `restoreGeometry()`。

优化前只检查 `restoreGeometry()` 的返回值，没有检查恢复后的矩形是否仍位于当前屏幕内。如果配置来自其他分辨率、其他显示器或竖屏环境，恢复后的窗口可能：

- 宽度或高度超过当前屏幕；
- 位于负坐标；
- 只有一部分位于可见区域；
- 保留旧的参数栏比例和窗口布局状态。

### 2.8 Qt 5 高 DPI 属性未显式初始化

项目需要兼容 Qt 5.15 环境，但优化前没有在创建 `QApplication` 之前设置 Qt 5 的高 DPI 属性。

如果没有显式启用：

```cpp
Qt::AA_EnableHighDpiScaling
Qt::AA_UseHighDpiPixmaps
```

Qt 5 板端可能使用不同的默认缩放策略，导致字体、图标、控件尺寸和窗口逻辑尺寸与开发机不一致。

### 2.9 没有屏幕和 DPI 变化处理

优化前主窗口没有监听：

- 当前屏幕变化；
- 屏幕分辨率变化；
- 屏幕可用区域变化；
- 逻辑 DPI 变化；
- 全屏和普通窗口状态变化。

因此，即使启动时尺寸正常，后续切换显示器、改变缩放或退出全屏后，也可能保留旧布局状态。

## 3. 优化目标

本轮优化围绕以下目标实施：

1. 窗口不再依赖固定的 `1440×860`；
2. 参数栏不再固定为 `340 px`；
3. 频谱区在正常横屏下优先获得空间；
4. 窄屏和竖屏环境下自动切换布局；
5. 表单在窄宽度下能够换行，不再依赖隐藏溢出来“维持外观”；
6. 恢复旧窗口几何前检查其可见性；
7. 兼容屏幕分辨率、可用区域和 DPI 的运行时变化；
8. 兼容 Qt 5.15 的高 DPI 初始化方式；
9. 增加可部署到开发板的显示环境诊断日志；
10. 保持频谱、瀑布图、Marker 和采集控制功能不变。

## 4. 具体修改内容

### 4.1 主区域改为可调分栏

文件：

- `src/ui/MainWindow.cpp`
- `src/ui/MainWindow.h`

主界面新增水平 `QSplitter`，用于管理“绘图区”和“参数控制栏”：

```text
MainWindow
└── mainSplitter_ (QHorizontalSplitter)
    ├── plotSplitter_ (QVerticalSplitter)
    │   ├── SpectrumPlotWidget
    │   └── WaterfallPlotWidget
    └── controlPanel_
```

主要行为：

- `mainSplitter_` 设置 `childrenCollapsible=false`，避免用户或布局系统意外折叠关键区域；
- 频谱区设置为拉伸因子 `1`；
- 参数栏设置为拉伸因子 `0`；
- 参数栏宽度改为可调，而不是固定像素值。

优化前后对比：

| 项目 | 优化前 | 优化后 |
|---|---|---|
| 主布局 | `QHBoxLayout` 固定左右排列 | `QSplitter(Qt::Horizontal)` |
| 参数栏 | 固定 `340 px` | 横屏下 `300-400 px` |
| 频谱区 | 被动压缩 | 优先拉伸，参数栏不抢占额外空间 |
| 用户调整 | 不支持 | 可通过分隔条调整 |

### 4.2 参数栏宽度范围化

参数栏在构建时设置：

```cpp
panel->setMinimumWidth(300);
panel->setMaximumWidth(400);
panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
```

在宽屏布局下，参数栏宽度按可用宽度计算，并限制在 `300-400 px`：

```cpp
const int panelWidth =
    std::clamp(availableWidth * 23 / 100, 300, 400);
```

这样可以保证：

- 窗口较宽时，参数栏不会无限扩张；
- 频谱区始终保留主要横向空间；
- 窗口变窄时，参数栏不会继续保持过大的固定宽度。

### 4.3 增加响应式布局断点

新增 `applyResponsiveLayout()`，根据当前窗口实际尺寸选择布局方向：

```cpp
const bool compact =
    width() < 1180 || height() > width() * 1.15;
```

其中：

- `width() < 1180` 表示逻辑宽度不足以稳定承载横向双栏；
- `height() > width() * 1.15` 表示窗口已明显呈竖屏或高纵横比。

布局切换规则：

| 条件 | 布局方向 | 参数栏策略 |
|---|---|---|
| 横屏且宽度不小于 `1180 px` | 水平布局 | 左侧频谱，右侧参数栏，宽度 `300-400 px` |
| 宽度小于 `1180 px` | 垂直布局 | 上方频谱，下方参数区 |
| 高度大于宽度的 `1.15` 倍 | 垂直布局 | 上方频谱，下方参数区 |

紧凑布局中参数区高度限制在 `220-420 px`，避免同时压缩频谱和瀑布图。

### 4.4 表单启用自动换行

新增 `configureFormWrapPolicy(bool compact)`，统一遍历参数栏内的 `QFormLayout`：

```cpp
form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
form->setRowWrapPolicy(
    compact ? QFormLayout::WrapAllRows
            : QFormLayout::WrapLongRows);
form->invalidate();
```

两种策略的作用：

- 宽屏使用 `WrapLongRows`：只有长行换行，保持参数栏紧凑；
- 紧凑布局使用 `WrapAllRows`：标签在上、输入控件在下，避免横向挤压。

修改完成后重新调用 `updateGeometry()`，确保滚动区和父布局重新计算尺寸。

### 4.5 滚动区从“隐藏溢出”改为“允许收缩和按需滚动”

频率与幅度、迹线与瀑布、模拟信号源和标记页面的策略统一调整：

```cpp
scroll->setWidgetResizable(true);
scroll->setFrameShape(QFrame::NoFrame);
scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
```

页面同时设置：

```cpp
page->setMinimumWidth(0);
page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
```

该修改包含两层保障：

1. 正常情况下，通过表单换行和放宽尺寸策略消除横向溢出；
2. 极端字体或 DPI 环境下，仍允许出现水平滚动条，保证内容可访问，而不是直接裁切。

原有的 `NoHorizontalScrollArea` 自定义类已移除，避免通过重写 `scrollContentsBy()` 强制丢弃水平滚动。

### 4.6 参数页和 Tab 控件允许收缩

`mainTabWidget_`、各参数页和模拟信号源页面均设置最小宽度为 `0`，并采用可压缩的横向尺寸策略。

目的是避免子控件的 `sizeHint()` 通过布局链反向撑大整个参数栏，导致主窗口无法按屏幕尺寸缩小。

### 4.7 频率单位下拉框自适应字体宽度

文件：`src/ui/FrequencySpinBox.cpp`

优化前：

```cpp
unitCombo_->setFixedWidth(64);
```

优化后：

```cpp
const int unitTextWidth =
    unitCombo_->fontMetrics().horizontalAdvance(QStringLiteral("GHz"));

unitCombo_->setMinimumWidth(
    std::clamp(unitTextWidth + 30, 52, 64));
unitCombo_->setMaximumWidth(
    std::clamp(unitTextWidth + 44, 64, 80));
unitCombo_->setSizePolicy(
    QSizePolicy::Preferred, QSizePolicy::Fixed);
```

这样单位控件会随字体度量调整，同时保持合理上下限，不挤压频率数值输入框。

### 4.8 窗口启动尺寸绑定屏幕可用区域

新增 `applyInitialWindowGeometry()`。

处理流程：

1. 获取当前屏幕；
2. 读取 `availableGeometry()`；
3. 四周保留 `8 px` 安全边距；
4. 如果屏幕过小，则退回未收缩的可用区域；
5. 优先窗口尺寸 `1440×860` 与可用区域求交；
6. 将窗口居中到可用区域；
7. 如果存在恢复几何，则先检查其是否仍与某个屏幕有足够交集。

恢复几何的可见性条件为：

```text
当前窗口与任一屏幕可用区域的交集宽度 >= min(320, 当前宽度)
当前窗口与任一屏幕可用区域的交集高度 >= min(200, 当前高度)
```

如果旧几何不满足条件，不再直接沿用，而是重新选择安全尺寸和位置。

### 4.9 增加窗口边界纠偏

新增 `ensureWindowWithinScreen()`，用于处理窗口大小和位置越界。

与普通 `resize()` 不同，该函数考虑窗口框架装饰：

- 使用 `frameGeometry()` 获取包含标题栏和边框的窗口外框；
- 计算 `frameGeometry().size() - size()` 得到窗口装饰尺寸；
- 根据可用屏幕区域计算最大客户区尺寸；
- 根据完整外框计算允许的最大左上角坐标；
- 使用 `std::clamp()` 将窗口框架限制在屏幕可用区域内。

这样可以避免“客户区看起来在屏幕内，但标题栏或边框仍超出 framebuffer”的情况。

判断越界时同时考虑：

- 左边界；
- 上边界；
- 右边界；
- 下边界。

### 4.10 显示后延迟复检

在 `showEvent()` 中执行首次窗口边界检查，并通过零延迟定时器再执行一次复检：

```cpp
ensureWindowWithinScreen();
QTimer::singleShot(0, this, [this] {
    ensureWindowWithinScreen();
    scheduleResponsiveLayout();
});
```

原因是某些窗口系统在第一次显示前还不能提供最终窗口装饰尺寸。延迟复检可以在原生窗口创建后再修正一次。

该处理对 `linuxfb` 通常不会产生额外副作用；如果 QPA 不支持窗口移动，窗口仍会使用实际 framebuffer 尺寸进行布局。

### 4.11 监听屏幕和 DPI 变化

窗口首次显示时建立以下信号连接：

- `QWindow::screenChanged`；
- `QWindow::windowStateChanged`；
- `QScreen::geometryChanged`；
- `QScreen::availableGeometryChanged`；
- `QScreen::logicalDotsPerInchChanged`。

触发后执行：

- 重新评估响应式断点；
- 重新约束窗口边界；
- 更新全屏按钮状态；
- 在必要时重新计算分栏尺寸。

布局更新通过一个 `60 ms` 单次定时器合并，避免窗口连续缩放时反复计算整个布局。

### 4.12 补齐 Qt 5.15 高 DPI 初始化

文件：`src/app/main.cpp`

在创建 `QApplication` 之前增加：

```cpp
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
```

Qt 6 默认启用高 DPI 支持，因此仅对 Qt 5 设置这些属性。

板端仍需确认启动脚本没有同时叠加多个缩放来源，例如：

- `QT_SCALE_FACTOR`；
- `QT_SCREEN_SCALE_FACTORS`；
- `QT_AUTO_SCREEN_SCALE_FACTOR`；
- `QT_FONT_DPI`；
- 桌面环境自身缩放。

### 4.13 修复全屏按钮状态同步

优化前，从全屏调用 `showNormal()` 后，按钮可能仍显示“退出全屏”。

本轮在以下位置同步按钮文本：

- `changeEvent()` 的 `WindowStateChange`；
- `resizeEvent()`；
- 零延迟事件队列；
- `QWindow::windowStateChanged`。

按钮文本规则：

```text
全屏状态 -> “退出全屏”
普通状态 -> “进入全屏”
```

### 4.14 增加 UI 环境诊断日志

窗口首次显示时输出一行 `UI environment` 日志，包括：

- Qt 版本；
- QPA 平台；
- 屏幕名称；
- 屏幕物理几何；
- 屏幕可用几何；
- `devicePixelRatio`；
- 逻辑 DPI；
- 物理 DPI；
- `QGuiApplication::font()`；
- 启动脚本中的 Qt 缩放环境变量；
- 主窗口 `frameGeometry()`；
- 当前布局模式；
- 参数栏尺寸；
- 频谱绘图区尺寸。

示例：

```text
UI environment: qt=6.10.2 qpa=windows screen=\\.\DISPLAY1
geometry=QRect(0,0 1707x1067)
available=QRect(0,0 1707x1019)
dpr=1.5 logicalDpi=96 physicalDpi=126.047
font=Microsoft YaHei UI,9
window=QRect(134,80 1440x890)
layout=wide panel=QSize(300,799) plot=QSize(1122,488)
```

该日志可直接用于区分：

- 窗口越界；
- 逻辑分辨率不足；
- 高 DPI 二次缩放；
- 字体度量异常；
- QPA 不报告正确屏幕尺寸；
- 参数栏或绘图区尺寸分配异常。

## 5. 修改文件汇总

| 文件 | 修改内容 |
|---|---|
| `src/ui/MainWindow.h` | 新增屏幕、布局、窗口几何和诊断相关成员及方法声明 |
| `src/ui/MainWindow.cpp` | 主分栏、响应式布局、表单换行、滚动策略、窗口边界、屏幕信号、环境日志、全屏状态同步 |
| `src/ui/FrequencySpinBox.cpp` | 单位下拉框按字体度量自适应宽度 |
| `src/app/main.cpp` | Qt 5 高 DPI 属性初始化 |
| `tests/tst_MainWindow.cpp` | 新增 2K、竖屏、横屏恢复和表单换行策略测试 |
| `其他材料/UI兼容性.md` | 问题分析、解决思路和实施结果记录 |

## 6. 优化前后行为对比

| 场景 | 优化前 | 优化后 |
|---|---|---|
| 2K 高缩放逻辑宽度不足 | 固定窗口可能越界 | 根据可用区域缩小并居中 |
| 右侧参数栏 | 固定 `340 px` | `300-400 px`，频谱区优先 |
| 竖屏或窄窗口 | 始终左右排列 | 自动切换上下排列 |
| 表单标签较长 | 依赖默认布局，可能裁切 | 宽屏长行换行，紧凑布局全部上下换行 |
| 内容横向溢出 | 强制隐藏滚动条 | 优先换行，必要时允许滚动 |
| 单位下拉框 | 固定 `64 px` | 根据字体自适应 |
| 旧窗口配置 | 直接恢复 | 先检查可见性，再决定是否使用 |
| 屏幕或 DPI 改变 | 不处理 | 重新计算布局和窗口边界 |
| Qt 5 高 DPI | 未显式启用 | 创建 `QApplication` 前启用 |
| 问题定位 | 无环境日志 | 输出完整 `UI environment` 信息 |

## 7. 验证结果

### 7.1 编译验证

使用以下环境完成编译：

```text
Qt 6.10.2
MinGW 64-bit
Windows
```

结果：编译成功，无新增编译错误。

### 7.2 GUI 自动化测试

执行：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
& 'build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug\tests\rtsa_gui_tests.exe'
```

结果：

```text
29 passed, 0 failed
```

新增布局测试覆盖：

- `2560×1440` 横屏下使用水平布局；
- 参数栏宽度保持在 `300-400 px`；
- 频谱区宽度明显大于参数栏；
- `1000×1200` 下切换为垂直布局；
- 恢复 `2560×1440` 后重新切换为水平布局；
- 宽屏使用 `WrapLongRows`；
- 紧凑布局使用 `WrapAllRows`。

### 7.3 性能回归

1920×1080 CPU Raster 端到端测试结果：

| 指标 | 结果 |
|---|---|
| 可见界面帧率 | 约 `53.3 FPS` |
| 最后帧绘制时间 | 约 `0.07 ms` |
| 可见帧延迟 P95 | 约 `17.4 ms` |

布局修改没有导致频谱绘制性能明显下降。

### 7.4 本机高 DPI 运行验证

本机 Windows 环境：

| 项目 | 数值 |
|---|---|
| Qt 版本 | `6.10.2` |
| QPA | `windows` |
| 逻辑屏幕尺寸 | `1707×1067` |
| 可用区域 | `1707×1019` |
| `devicePixelRatio` | `1.5` |
| 窗口模式框架尺寸 | `1440×890` |
| 全屏参数栏宽度 | `300 px` |
| 全屏频谱绘图区宽度 | `1389 px` |

窗口模式能够完整位于可用区域内；全屏模式下频谱区重新获得主要宽度。

### 7.5 完整 CTest 结果

完整 CTest 结果为：

```text
14 / 15 passed
```

唯一失败项：

```text
rtsa_plot_tests::appearanceSettingsChangeRasterOutput
期望背景色：#e8ecf0
实际背景色：#efeff7
```

该失败项位于绘图组件颜色断言，不涉及：

- `MainWindow`；
- 响应式布局；
- 窗口边界；
- 参数栏宽度；
- 表单换行；
- 高 DPI 初始化。

该问题属于本次 UI 兼容性修改前已有的颜色断言差异，本轮未修改相关绘图颜色逻辑。

## 8. 当前结论与适用边界

本轮修改已经消除了当前能够从源码和截图中确认的主要固定布局缺陷，并且在本机 Windows Qt 6.10.2 环境下通过了自动化测试和实际高 DPI 运行验证。

但当前结果不能等价为“已经保证在 ZU47DR 上正常显示”，原因如下：

1. 当前编译和运行环境是 Windows Qt 6.10.2，不是开发板的 Qt 5.15、ARM 交叉编译环境；
2. 当前没有在 `linuxfb`、XCB 或 Wayland QPA 上实际运行；
3. `2560×1440` 自动化用例是在离屏平台上通过 `resize()` 模拟，离屏虚拟屏幕本身仍为 `800×800`；
4. 开发板使用的中文字体、字体回退和字体度量可能不同；
5. 板端启动脚本可能叠加多个高 DPI 缩放变量；
6. 没有窗口管理器时，QPA 对 `move()`、`availableGeometry()` 和全屏切换的处理可能与 Windows 不同。

因此当前状态应描述为：

> 已完成针对已知 UI 兼容性根因的修复，本机验证通过，预计能够解决开发板 2K 显示中的窗口越界、参数栏过宽、频谱压缩和表单裁切问题；仍需在目标板上完成最终验收后，才能确认板端兼容性。

## 9. 开发板验收方案

### 9.1 推荐启动方式

开发板部署时建议优先使用：

```sh
./rtsa_app --source dma --fullscreen
```

无窗口管理器的嵌入式环境使用全屏模式，可以避免窗口位置和窗口装饰差异，同时使内部布局直接基于 framebuffer 实际尺寸计算。

首次验收建议使用干净的配置目录：

```sh
mkdir -p /tmp/rtsa-ui-test
XDG_CONFIG_HOME=/tmp/rtsa-ui-test ./rtsa_app --source dma --fullscreen
```

### 9.2 需要采集的信息

从程序日志中提取：

```text
UI environment:
qt=
qpa=
screen=
geometry=
available=
dpr=
logicalDpi=
physicalDpi=
font=
scaleFactor=
screenScaleFactors=
autoScreenScale=
fontDpi=
window=
layout=
panel=
plot=
```

同时记录：

- 启动脚本内容；
- 显示器物理分辨率；
- 屏幕旋转方向；
- 是否使用旧 `QSettings`；
- 当前界面截图。

### 9.3 验收矩阵

| 测试项 | 测试条件 | 通过标准 |
|---|---|---|
| 2K 原生缩放 | `2560×1440`，100% | 窗口不越界，参数标签完整 |
| 2K 125% 缩放 | `2560×1440`，125% | 不发生二次放大或控件重叠 |
| 2K 150% 缩放 | `2560×1440`，150% | 表单自动换行，频谱区大于 70% |
| 窗口恢复 | 使用旧配置启动 | 旧几何不可见时自动回到屏幕内 |
| 窗口缩放 | 连续拉宽、拉窄 | 横竖布局在断点处正常切换 |
| 全屏切换 | 进入全屏、退出全屏 | 布局和按钮状态均正确 |
| 竖屏 | 实际支持时旋转屏幕 | 自动使用上下布局 |
| QPA | `linuxfb`/XCB/Wayland | 至少在实际部署后端下通过 |

### 9.4 板端日志判断

正常情况应满足：

- `geometry` 和 `available` 数值合理；
- `dpr` 与启动脚本缩放策略一致；
- 横屏下 `layout=wide`；
- 参数栏宽度在 `300-400 px`；
- 频谱区宽度约占窗口宽度 `70%` 以上；
- 表单没有不可访问的横向裁切；
- 界面没有负坐标或窗口框架越过屏幕边界。

如果日志显示 `geometry` 已经是异常逻辑尺寸，应优先检查启动脚本中的 Qt 缩放变量，而不是继续调整布局常量。

## 10. 后续可选优化

如果板端验收仍发现显示器热插拔、屏幕移除或 QPA 延迟上报尺寸的问题，可以继续增加：

1. `QGuiApplication::screenAdded` 和 `screenRemoved` 的监听；
2. 首次绘制完成后的第二次延迟布局复检；
3. 板端专用的 `--ui-diagnostics` 参数，将环境信息写入独立文件；
4. 将参数栏最小宽度、断点阈值和边距统一提取为命名常量；
5. 在真实板端 Qt 5.15 工具链中增加编译验证。

这些项目目前不是已知问题的必要条件，应根据板端实际日志和截图决定是否实施。

## 11. 总结

本次优化将 UI 布局从“依赖开发机固定尺寸”改为“依赖当前窗口和屏幕的实际可用空间”，主要完成了：

- 可调主分栏；
- 自适应参数栏；
- 窄屏和竖屏断点；
- 表单换行与滚动兜底；
- 窗口几何校验和边界纠偏；
- 屏幕及 DPI 变化响应；
- Qt 5 高 DPI 初始化；
- 全屏状态同步；
- 可现场采样的显示环境日志。

代码层面的已知问题已经修复，本机回归验证通过。最终能否在嵌入式系统上稳定显示，仍必须在 ZU47DR 的真实 framebuffer、字体、Qt 版本和 QPA 环境中完成验收。
