# RTSA 开发板 2K 显示 UI 兼容性分析与解决方案

| 项目 | 内容 |
|---|---|
| 文档版本 | V1.0 |
| 编写日期 | 2026-10-26 |
| 适用版本 | 当前 RTSA 工作区源码 |
| 问题场景 | Windows 开发机显示正常，部署到 ZU47DR 开发板并使用 2K 显示器后布局异常 |
| 结论状态 | 已完成代码修复并通过 Windows Qt 6.10.2 自动化验证；板端实际 QPA 与缩放组合仍需现场复测 |

## 1. 结论摘要

该问题属于 **UI 布局与高 DPI/屏幕适配问题**，不是频谱数据、DMA 接收或 CPU Raster 绘制本身失效。

从截图中可以确认：

- 右侧参数控制区占据了过大比例；
- 左侧频谱绘图区被压缩成窄条；
- 部分分组标题、表单标签和控件左侧内容被裁切；
- 界面仍能显示频谱、采集按钮和频率数值，说明应用主体已经运行，故障集中在大窗口布局、控件尺寸和屏幕适配。

修复前源码中有四个直接相关的风险点：

1. 主窗口启动尺寸固定为 `1440×860`；
2. 右侧控制面板固定为 `340 px`；
3. 主区域始终采用“左侧频谱 + 右侧参数栏”的横向布局，没有窄屏或竖屏断点；
4. 表单滚动区关闭水平滚动，但表单内容仍可能因字体和 DPI 变化超过可用宽度，造成标签或输入框不可访问。

Windows Qt 6 的窗口管理器、字体度量和逻辑像素尺寸恰好能容纳这些固定值，因此开发机上显示正常。开发板通常使用 Qt 5.15、`linuxfb` 或其他 QPA，且 2K 屏可能叠加 125%、150% 或 200% 缩放。逻辑分辨率、字体高度和控件最小尺寸发生变化后，上述固定布局假设就会失效。

## 2. 证据与边界

### 2.1 截图现象

用户提供的板端实拍图是局部竖幅照片，不能直接代表完整 framebuffer，但可以确认以下现象：

| 观察项 | 现象 | 判断 |
|---|---|---|
| 频谱区 | 仅剩左侧窄条，横轴和瀑布色标仍可见 | 绘图功能仍在，但可用宽度严重不足 |
| 参数区 | 右侧控制栏占据大部分可视宽度 | 固定宽度参数栏与主窗口比例失衡 |
| 文本 | “数据源与频率”等标题和左侧标签疑似被截断 | 控件内容超出父区域，或窗口左侧超出屏幕 |
| 操作区 | 采集、频率、频点数等主要控件仍可见 | 不是控件全部未创建，而是布局空间分配异常 |
| 显示状态 | 界面没有被整体旋转或花屏 | 更符合尺寸和缩放问题，而非 OpenGL/栅格后端损坏 |

照片没有提供以下关键信息，因此不能只凭图片确定唯一根因：

- 显示器物理分辨率是 `2560×1440`、`1920×1080` 还是其他“2K”规格；
- 是否旋转为竖屏；
- Qt 使用的逻辑分辨率；
- 是否存在 `QT_SCALE_FACTOR`、桌面环境缩放或 `QT_FONT_DPI`；
- 当前 QPA 是 `linuxfb`、XCB、Wayland 还是其他平台插件；
- 应用报告的实际窗口尺寸与屏幕可用区域。

### 2.2 修复前代码证据

下表记录 V0.1 分析时的问题代码，用于说明根因；V1.0 的实际修复见第 9 节。

| 文件与位置 | 修复前实现 | 风险 |
|---|---|---|
| `src/ui/MainWindow.cpp:169` | `resize(1440, 860);` | 没有根据屏幕可用区域缩小，逻辑分辨率不足时窗口会超出屏幕 |
| `src/ui/MainWindow.cpp:1783` | `panel->setFixedWidth(340);` | 参数栏完全不能随字体、DPI 和窗口宽度调整 |
| `src/ui/MainWindow.cpp:1638-1661` | 主界面固定使用横向 `QHBoxLayout` | 没有窄屏、竖屏或低逻辑分辨率布局 |
| `src/ui/MainWindow.cpp:1720-1724` | 参数页使用 `QScrollArea`，且水平滚动条永久关闭 | 内容宽度超过视口时，超出部分可能完全不可访问 |
| `src/ui/MainWindow.cpp:1741-1745` | 迹线与瀑布页采用相同策略 | 同样存在横向裁切风险 |
| `src/ui/MainWindow.cpp:2579` | 直接 `restoreGeometry()` | 未检查恢复后的窗口是否仍在当前屏幕可见范围内 |
| `src/ui/FrequencySpinBox.cpp:50` | 单位下拉框固定宽度 `64 px` | DPI 或字体变化后，输入框可用宽度进一步减少 |
| `src/app/main.cpp:41` | 创建 `QApplication` 前未设置 Qt 5 高 DPI 属性 | Qt 5.15 板端可能采用与开发机不同的缩放策略 |
| `src/ui/ApplicationTheme.cpp` | 多处使用固定像素字号、内边距和最小高度 | 字体度量变化时，控件的实际最小尺寸可能超出预期 |

## 3. 根因分析

### 3.1 固定窗口尺寸与固定侧栏宽度

修复前窗口默认按 `1440×860` 创建，参数栏始终宽度为 `340 px`。这意味着：

- 在 `2560×1440@100%` 下，窗口看起来可能正常；
- 在 2K 屏开启 150% 缩放后，Qt 看到的逻辑宽度可能只有约 `1707 px`，仍可能勉强容纳；
- 在 200% 缩放或 `1920×1080@150%` 下，逻辑宽度约为 `1280 px`，固定窗口会超过屏幕；
- 如果屏幕旋转为竖屏，`1440×2560` 的物理宽度在缩放后可能小于窗口最小需求。

当窗口宽度大于 framebuffer 或窗口管理器可用区域时，`linuxfb` 没有桌面窗口管理器自动纠正位置，窗口可能从负坐标开始或直接越过屏幕边界。这会同时造成频谱左侧和参数标签被裁切。

### 3.2 布局没有自适应断点

修复前界面没有根据宽高比切换布局。即使参数栏把频谱压到很窄，应用仍坚持左右并排。

在仪器面板中，频谱绘图区是主要信息载体，参数栏是辅助操作区。当前布局没有设置“绘图区最小可接受宽度”，导致参数栏在视觉优先级上超过了频谱区。

### 3.3 表单横向溢出被隐藏

修复前频率与幅度页使用 `QScrollArea` 并设置：

```cpp
setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
```

这只是隐藏滚动条，并不会让内部 `QFormLayout` 自动变窄。当字体变大、DPI 改变或单位控件变宽时，页面内容的最小宽度可能超过视口。此时：

- 内容可能被直接裁切；
- 焦点切换到右侧输入框时，`QScrollArea` 可能自动调整内容位置，使左侧标签不可见；
- 用户无法通过水平滚动找回归一化后的标签。

这与截图中左侧标签疑似缺失的现象一致。

### 3.4 Qt 5 高 DPI 与板端字体差异

项目要求兼容 Qt 5.15，但 `QApplication` 创建前没有显式设置 Qt 5 高 DPI 属性。开发板还可能存在以下差异：

- `linuxfb` 报告的物理 DPI 与显示器实际 DPI 不一致；
- 系统字体、中文字体回退和字体度量与 Windows 不同；
- 启动脚本重复设置 `QT_SCALE_FACTOR`、`QT_SCREEN_SCALE_FACTORS` 或 `QT_FONT_DPI`；
- Qt 5 自动缩放和手动缩放叠加，形成“二次放大”；
- 指针、按钮和表单行高变大，但窗口和侧栏仍使用开发机固定尺寸。

因此，“电脑正常”并不能证明布局具备可移植性，只能说明当前布局恰好适配 Windows Qt 6 的字体和逻辑像素环境。

### 3.5 历史窗口几何被恢复

修复前程序会把窗口几何保存到 `QSettings`，启动时直接恢复。若配置来自其他分辨率或屏幕方向，恢复后的窗口可能：

- 宽高超过当前屏幕；
- 位于屏幕可见区域之外；
- 保留旧的参数栏宽度和分栏位置。

当前代码只判断 `restoreGeometry()` 的返回值，没有检查恢复后的矩形是否与当前屏幕有效区域相交。

## 4. 推荐解决方案

建议按“先现场定位、再应急规避、最后代码根治”的顺序处理，不建议通过简单缩小字体掩盖问题。

本节保留修复设计与论证过程；其中窗口约束、可调参数栏、响应式断点、表单换行、滚动策略和 Qt 5 高 DPI 初始化已在 V1.0 实施，具体结果见第 9 节。

### 4.1 第一步：采集板端显示环境

在开发板上执行并保存以下信息：

```sh
env | grep -E '^(QT_|DISPLAY|WAYLAND_DISPLAY|XDG_SESSION_TYPE)=' || true
cat /sys/class/graphics/fb0/virtual_size 2>/dev/null || true
fbset -i 2>/dev/null || true
```

如果使用 X11，可补充：

```sh
xrandr --current
```

同时在程序中记录 Qt 的真实屏幕参数：

```cpp
QScreen* screen = window.windowHandle()
    ? window.windowHandle()->screen()
    : QGuiApplication::primaryScreen();

if (screen) {
    qInfo().noquote()
        << "screen=" << screen->name()
        << "geometry=" << screen->geometry()
        << "available=" << screen->availableGeometry()
        << "dpr=" << screen->devicePixelRatio()
        << "logicalDpi=" << screen->logicalDotsPerInch()
        << "physicalDpi=" << screen->physicalDotsPerInch()
        << "font=" << QGuiApplication::font().toString();
}
```

需要重点确认：

- `geometry` 是否为 `2560×1440`；
- 是否被缩放为 `1707×960`、`1280×720` 或其他逻辑尺寸；
- `devicePixelRatio` 是否为 `1.0`；
- 逻辑 DPI 是否异常大；
- 启动脚本是否存在重复缩放变量。

### 4.2 第二步：现场应急验证

这些操作只用于快速确认问题来源，不是最终代码修复。

#### 方案 A：使用干净配置启动

避免旧的窗口几何和设置在板端生效：

```sh
XDG_CONFIG_HOME=/tmp/rtsa-ui-test \
QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 \
QT_SCALE_FACTOR=1 \
./rtsa_app --source dma --fullscreen
```

如果干净配置下布局恢复正常，问题主要来自保存的窗口几何或配置迁移。

#### 方案 B：清理缩放环境变量

先排除重复缩放：

```sh
env -u QT_SCALE_FACTOR \
    -u QT_SCREEN_SCALE_FACTORS \
    -u QT_AUTO_SCREEN_SCALE_FACTOR \
    QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 \
    ./rtsa_app --source dma --fullscreen
```

如果需要放大字体，只保留一种缩放策略。不要同时启用自动缩放、手动缩放和自定义字体 DPI。

#### 方案 C：改用可用的窗口系统后端

如果板端 rootfs 已提供 XCB 或 Wayland，分别测试：

```sh
QT_QPA_PLATFORM=xcb ./rtsa_app --source dma
QT_QPA_PLATFORM=wayland ./rtsa_app --source dma
```

有窗口管理器时通常能自动约束窗口位置，但最终仍应修复固定尺寸布局。

### 4.3 第三步：代码根治

#### 4.3.1 窗口尺寸按屏幕可用区域收敛

不要无条件使用 `1440×860`。启动时读取当前屏幕可用矩形，并在屏幕内选择窗口尺寸：

```cpp
void MainWindow::fitWindowToScreen()
{
    QScreen* screen = windowHandle()
        ? windowHandle()->screen()
        : QGuiApplication::primaryScreen();
    if (!screen) {
        return;
    }

    const QRect available = screen->availableGeometry().adjusted(8, 8, -8, -8);
    const QSize preferred(1440, 860);
    const QSize target = preferred.boundedTo(available.size());

    resize(target);
    move(available.center() - rect().center());
}
```

对于嵌入式全屏运行，应优先使用 `showFullScreen()`，并让所有内部布局基于 `availableGeometry()` 或当前窗口实际尺寸计算，而不是依赖 1920×1080 或 2560×1440 的固定值。

#### 4.3.2 恢复旧窗口几何前检查可见性

只有在恢复结果完整位于当前屏幕内时才使用旧几何：

```cpp
bool savedGeometryFitsCurrentScreen(QRect frame)
{
    return std::any_of(
        QGuiApplication::screens().cbegin(),
        QGuiApplication::screens().cend(),
        [frame](const QScreen* screen) {
            const QRect available = screen->availableGeometry();
            return available.contains(frame)
                || available.intersected(frame).width() >= 320;
        });
}
```

如果不满足条件，调用 `fitWindowToScreen()`，不要继续使用旧矩形。

#### 4.3.3 参数栏改为可调宽度

删除固定宽度：

```cpp
panel->setFixedWidth(340);
```

改为有上下限的可调宽度：

```cpp
panel->setMinimumWidth(300);
panel->setMaximumWidth(380);
```

并使用横向 `QSplitter` 管理频谱区和参数区：

```cpp
auto* mainSplitter = new QSplitter(Qt::Horizontal, central);
mainSplitter->addWidget(plotSplitter_);
mainSplitter->addWidget(buildControlPanel());
mainSplitter->setStretchFactor(0, 1);
mainSplitter->setStretchFactor(1, 0);
mainSplitter->setSizes({ 1000, 340 });
```

初始参数栏宽度可以根据可用宽度计算：

```cpp
const int panelWidth = qBound(300, available.width() * 22 / 100, 380);
mainSplitter->setSizes({ available.width() - panelWidth, panelWidth });
```

#### 4.3.4 增加窄屏和竖屏断点

建议至少区分两种布局：

| 条件 | 布局 |
|---|---|
| 横屏且逻辑宽度不小于 1280 | 左侧频谱，右侧参数栏 |
| 竖屏或逻辑宽度小于 1280 | 上方频谱，下方可折叠参数区；或参数区改为抽屉/浮层 |

不建议在窄屏下继续压缩右侧参数栏，因为参数栏已经包含频率输入、单位下拉框、快捷频宽按钮和多行表单。

实现时可以在 `resizeEvent()` 或屏幕变化信号中重新计算：

```cpp
void MainWindow::adaptLayoutToWindow()
{
    const bool compact =
        width() < 1280 || height() > width() * 1.2;

    mainSplitter_->setOrientation(
        compact ? Qt::Vertical : Qt::Horizontal);
}
```

如果暂时不实现完整断点，最低限度也应让主分栏可拖动，并保证频谱区不会被压缩到无法读图的程度。

#### 4.3.5 表单改为自动换行或纵向排布

所有 `QFormLayout` 建议设置：

```cpp
form->setRowWrapPolicy(QFormLayout::WrapLongRows);
form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
```

窄屏时可以切换为“标签在上、控件在下”：

```cpp
form->setRowWrapPolicy(QFormLayout::WrapAllRows);
```

快速频宽按钮当前按 4 列排布。当参数栏小于约 `320 px` 或字体较大时，应改为 3 列或 2 列，不能让按钮互相挤压或溢出。

#### 4.3.6 滚动区不能只隐藏横向滚动条

推荐优先消除横向溢出，同时保留“需要时可滚动”的兜底：

```cpp
scroll->setWidgetResizable(true);
scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
```

同时确保页面允许收缩：

```cpp
page->setMinimumWidth(0);
page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
```

如果设计要求绝不出现水平滚动条，就必须保证所有表单在最小支持宽度下通过换行完整显示，不能仅使用 `ScrollBarAlwaysOff`。

#### 4.3.7 高频单位控件和标签宽度改为自适应

当前单位下拉框固定为 `64 px`：

```cpp
unitCombo_->setFixedWidth(64);
```

建议改为：

```cpp
unitCombo_->setMinimumWidth(48);
unitCombo_->setMaximumWidth(64);
unitCombo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
```

表单标签应允许换行或使用较短文本，不能依赖固定宽度截断。

#### 4.3.8 补齐 Qt 5 高 DPI 初始化

如果板端使用 Qt 5.15，应在创建 `QApplication` 之前设置：

```cpp
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication application(argc, argv);
```

Qt 6 默认启用高 DPI，不需要重复设置。启用后还要确保启动脚本没有额外叠加 `QT_SCALE_FACTOR`，否则可能发生二次缩放。

#### 4.3.9 像素常量改为统一的 UI 比例策略

当前多处样式直接使用 `11 px`、`12 px`、`13 px`、`34 px`、`38 px` 等固定值。建议：

- 字体使用统一的 `QFont` 配置或主题变量；
- 行高由 `QFontMetrics` 和样式最小高度共同决定；
- 只对触摸操作保留最小点击尺寸，不对整个窗口布局使用硬编码像素；
- 将参数栏宽度、间距、圆角和按钮高度集中为少量命名常量，便于目标板统一调整。

## 5. 推荐实施顺序

| 优先级 | 修改 | 预期收益 | 风险 |
|---|---|---|---|
| P0 | 启动时按屏幕可用区域限制窗口尺寸 | 避免窗口超出 framebuffer | 低 |
| P0 | 恢复旧几何前检查屏幕可见性 | 解决旧配置导致的黑边、偏移和裁切 | 低 |
| P0 | 参数栏由固定 `340 px` 改为可调宽度 | 保证频谱区有足够空间 | 低 |
| P1 | 表单启用 `WrapLongRows`，移除固定单位宽度 | 消除标签和输入框横向裁切 | 低 |
| P1 | 增加窄屏/竖屏布局断点 | 覆盖 2K 缩放和竖屏场景 | 中 |
| P1 | 补齐 Qt 5 高 DPI 初始化并规范启动脚本 | 避免字体和控件尺寸失控 | 中 |
| P2 | 统一字体、间距和最小尺寸策略 | 提高多分辨率可维护性 | 中 |

建议先完成 P0 和 P1 中低风险项目，再根据板端截图复测决定是否启用竖屏断点。不要一次性重做全部界面，否则会扩大回归范围。

## 6. 验收标准

至少在以下环境组合下验证：

- 物理分辨率 `2560×1440`，100% 缩放；
- 物理分辨率 `2560×1440`，125% 缩放；
- 物理分辨率 `2560×1440`，150% 缩放；
- 物理分辨率 `1920×1080`，100% 缩放；
- 开发板竖屏或窄逻辑宽度场景（如实际使用）；
- `linuxfb` 以及板端实际可用的 XCB/Wayland QPA。

验收条件：

1. 主窗口完整位于屏幕内，没有负坐标或越界；
2. 右侧参数栏全部控件可见，表单标签不被裁切；
3. 频谱区默认至少保留约 70% 的横向可用空间；
4. 正常尺寸下不出现水平滚动条；
5. 参数栏可以调整或折叠，频谱图不会被压成窄条；
6. 常用频宽按钮、频率输入框和单位下拉框均可操作；
7. 全屏、窗口模式、重启恢复几何均正常；
8. 使用干净 `QSettings` 和已有板端配置时都能正确显示；
9. 缩放比改变后不出现文字重叠、控件截断或点击区域错位；
10. 频谱、瀑布图、Marker、采集控制功能与修复前保持一致。

## 7. 建议的板端复测记录

每次测试建议记录以下内容：

```text
日期：
开发板：
显示器型号/接口：
物理分辨率：
旋转方向：
Qt 版本：
QPA：
QT_SCALE_FACTOR：
QT_SCREEN_SCALE_FACTORS：
QT_AUTO_SCREEN_SCALE_FACTOR：
QT_FONT_DPI：
QScreen::geometry：
QScreen::availableGeometry：
devicePixelRatio：
logicalDotsPerInch：
QGuiApplication::font：
主窗口启动尺寸：
主窗口 frameGeometry：
是否存在水平滚动：
截图：
```

如果问题复现，应同时提供：

- 开发板实拍或 framebuffer 截图；
- 启动脚本和环境变量；
- Qt 屏幕参数日志；
- 是否使用旧的 `QSettings` 配置；
- 当前 QPA 与 Qt 版本。

## 8. 结论

当前问题不是单一控件样式错误，而是“固定窗口 + 固定侧栏 + 强制横向布局 + 隐藏溢出 + 未验证高 DPI”共同造成的屏幕适配缺陷。Windows 环境只是在特定逻辑分辨率和字体度量下掩盖了这些假设。

最有效的修复顺序是：

1. 先按屏幕可用区域约束窗口；
2. 再把参数栏改为可调宽度并保证频谱区优先；
3. 然后修复表单换行和滚动策略；
4. 最后补齐 Qt 5 高 DPI 与板端 QPA 的启动规范。

完成这些修改后，窗口布局将不再依赖 Windows 开发机的固定尺寸，能够稳定适配 2K、缩放和竖屏等板端显示条件。

## 9. 已实施修复与验证结果

### 9.1 代码修改

本轮修复已在当前工作区实施，主要改动如下：

| 文件 | 已实施内容 |
|---|---|
| `src/ui/MainWindow.h` | 增加 `resizeEvent()`、`showEvent()`、屏幕边界约束、响应式布局和运行环境诊断接口 |
| `src/ui/MainWindow.cpp` | 主区域改为水平 `QSplitter`；参数栏改为 `300-400 px` 自适应宽度；窗口宽小于 `1180 px` 或明显竖屏时切换为上下布局；按当前屏幕可用区域约束并纠正窗口位置；检查恢复几何是否仍可见；监听屏幕和 DPI 变化；记录 Qt、QPA、分辨率、DPR、DPI、字体和窗口尺寸 |
| `src/ui/MainWindow.cpp` | 表单在宽屏使用 `WrapLongRows`，紧凑布局使用 `WrapAllRows`；滚动区改为 `ScrollBarAsNeeded`；页面允许横向收缩，避免通过隐藏滚动条造成不可访问裁切 |
| `src/ui/FrequencySpinBox.cpp` | 频率单位下拉框由固定 `64 px` 改为根据字体度量自适应 `52-80 px` |
| `src/app/main.cpp` | Qt 5.15 下在创建 `QApplication` 前启用 `AA_EnableHighDpiScaling` 和 `AA_UseHighDpiPixmaps` |
| `src/ui/MainWindow.cpp` | 修复全屏切换后按钮文本不能及时恢复的问题，并处理窗口状态变化与尺寸变化同步 |
| `tests/tst_MainWindow.cpp` | 增加 2K 横屏、竖屏、恢复横屏及表单换行策略的回归测试 |

启动时会输出一行 `UI environment` 诊断日志，包含 `geometry`、`available`、`devicePixelRatio`、逻辑/物理 DPI、Qt 缩放环境变量、窗口框架尺寸以及频谱区和参数栏尺寸。板端复测时应首先保存该日志，以区分布局问题、缩放问题和 QPA 后端问题。

### 9.2 自动化验证结果

在当前 Windows 开发环境完成以下验证：

- 工程使用 Qt 6.10.2 MinGW 64-bit 配置编译成功；
- `rtsa_gui_tests` 共 `29` 项全部通过，其中新增用例覆盖 `2560×1440` 横屏、`1000×1200` 竖屏、恢复横屏，以及参数栏宽度和表单换行策略；
- 1920×1080 CPU Raster 端到端测试达到约 `53.3 FPS`，最后帧绘制约 `0.07 ms`，可见帧延迟 P95 约 `17.4 ms`；
- 本机实际运行环境的 Windows 逻辑屏幕为 `1707×1067`，可用区域为 `1707×1019`，`devicePixelRatio=1.5`；
- 窗口模式启动时框架尺寸为 `1440×890`，完整位于可用区域内；
- 全屏模式下参数栏宽度为 `300 px`，频谱绘图区宽度为 `1389 px`，未再出现参数栏挤压频谱区的问题。

完整 CTest 结果为 `14/15`。唯一失败项为 `rtsa_plot_tests::appearanceSettingsChangeRasterOutput`：测试期望背景色 `#e8ecf0`，实际输出为 `#efeff7`。该用例不涉及 `MainWindow`、响应式布局或高 DPI 改动，属于本次修改前已存在的颜色断言差异，未在本轮 UI 兼容性修复中改动。

### 9.3 板端复测要求

部署到 ZU47DR 后，至少应验证以下组合：

1. `2560×1440`，100%、125% 和 150% 缩放；
2. 实际使用的全屏启动脚本；
3. 板端实际 QPA，例如 `linuxfb`、XCB 或 Wayland；
4. 使用干净 `QSettings` 和已有配置文件各启动一次；
5. 横屏与竖屏各切换一次，并确认参数标签、单位下拉框和频宽按钮无裁切；
6. 确认频谱区默认宽度不低于窗口宽度的约 70%，窗口框架不超出屏幕可用区域。

如果板端仍有异常，应提供 `UI environment` 日志、启动脚本中的 `QT_*` 环境变量、实际截图和 `QSettings` 文件。此时优先根据日志判断是窗口边界、逻辑分辨率、字体度量还是 QPA 后端限制，不应继续通过缩小字体或继续隐藏滚动条掩盖问题。
