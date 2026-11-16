# ZU47DR 板端显示与 GPU/QPA 核验指南

## 1. 文档目的

本文档用于回答一个必须在目标板上确认的问题：

> 当前 ZU47DR 板卡、PetaLinux/BSP、显示控制器和 Qt 部署环境，实际提供的是 LinuxFB、DRM/KMS、EGL/OpenGL ES，还是其他显示后端？

芯片系列和网上资料只能提供预期，不能替代板端证据。ZU47DR 常见配置没有 Mali GPU，但实际 BSP、设备树、驱动、Qt 插件和显示硬件仍应以目标板输出为准。核验完成前，不应把“无 GPU”“只能使用 CPU 渲染”或“切换某个 QPA 就能加速”写成已证实事实。

## 2. 先记录测试环境

在板卡串口或 SSH 中先采集基础信息：

```bash
uname -a
cat /etc/os-release
cat /proc/cpuinfo
cat /proc/device-tree/model 2>/dev/null
fbset 2>/dev/null
```

同时记录：

- RTSA 构建版本、构建日期和启动命令；
- Qt 版本及部署方式；
- 屏幕分辨率和刷新设置；
- `QT_QPA_PLATFORM`、`QT_QPA_FB_DRM`、`DISPLAY` 等环境变量；
- 测试窗口尺寸、频谱点数、输入帧率和显示视图模式。

这些信息应和性能结果一起保存。没有环境信息的 FPS 或延迟数字不能作为可复现结论。

## 3. 检查设备树和内核日志

### 3.1 设备树节点

```bash
find /proc/device-tree -type f \( -iname '*gpu*' -o -iname '*mali*' \) 2>/dev/null
find /proc/device-tree -type d \( -iname '*gpu*' -o -iname '*mali*' \) 2>/dev/null
```

如果存在 GPU 节点，继续查看节点状态和兼容字符串：

```bash
find /proc/device-tree -type d -iname '*gpu*' -print 2>/dev/null
```

对找到的目录检查其中的 `status`、`compatible`、`reg` 等文件。不要只看节点名字；`status` 为 `disabled` 或驱动未绑定，仍然不能视为可用图形加速。

### 3.2 内核日志

```bash
dmesg | grep -i -E 'mali|gpu|drm|framebuffer|fb0'
```

重点观察：

- GPU 驱动是否探测成功；
- 是否存在驱动固件、时钟、电源或 IOMMU 错误；
- DRM/KMS 驱动是否绑定到显示控制器；
- Framebuffer 是显示控制器输出、内存模拟，还是其他驱动创建。

仅出现 `fb0` 不代表没有 GPU；仅出现 DRM 节点也不自动代表存在 3D 加速。

## 4. 检查设备节点和系统类目录

```bash
ls -l /dev/fb* /dev/dri/* /dev/mali* 2>/dev/null
ls -l /sys/class/graphics 2>/dev/null
ls -l /sys/class/drm 2>/dev/null
```

需要区分：

- `/dev/fb0`：Framebuffer 接口；
- `/dev/dri/card*`：DRM 显示设备；
- `/dev/dri/renderD*`：DRM render node；
- `/dev/mali*`：特定 Mali 驱动节点；
- `/sys/class/drm/*`：DRM connector、encoder 和显示状态。

`/dev/dri/card0` 可能只是显示控制器，不能单独证明 GPU 可用。最终需要结合驱动名称、设备树、内核日志和 Qt QPA 运行结果判断。

## 5. 检查 Qt 平台插件

如果板端提供 Qt 工具，可查询部署信息：

```bash
qtpaths6 --qt-version 2>/dev/null
qtpaths6 --platforms 2>/dev/null
qtpaths --qt-version 2>/dev/null
qtpaths --platforms 2>/dev/null
```

不同 Qt 版本的工具名和参数可能不同，命令不存在时以实际发行版为准。

查看运行目录中的平台插件：

```bash
find /usr/lib /lib /opt -maxdepth 5 -type f \
  \( -name 'libqlinuxfb.so' -o -name 'libqeglfs.so' \
     -o -name 'libqkms.so' -o -name 'libqxcb.so' \) 2>/dev/null
```

启动 RTSA 前可保留插件加载日志：

```bash
export QT_DEBUG_PLUGINS=1
export QT_QPA_PLATFORM=linuxfb
./rtsa_app --source simulated
```

如果需要验证其他 QPA，应逐次只改变一个变量，并记录完整输出：

```bash
QT_QPA_PLATFORM=eglfs QT_QPA_EGLFS_DEBUG=1 ./rtsa_app --source simulated
QT_QPA_PLATFORM=kms QT_LOGGING_RULES='qt.qpa.*=true' ./rtsa_app --source simulated
```

这些命令是条件验证步骤，不表示目标板已经具备 `eglfs`、`kms` 或 GPU。插件加载失败、没有 `/dev/mali*`、没有 render node，或者 Qt 回退到 `linuxfb`，都必须原样记录。

## 6. 如何解释结果

### 6.1 可以支持“存在可用 GPU 路径”的证据

通常需要同时满足：

1. 设备树中存在已启用的 GPU 节点；
2. 内核日志显示 GPU 驱动成功探测并绑定；
3. 存在匹配的驱动节点或 DRM render node；
4. Qt 的 EGL/OpenGL ES 平台插件能够启动；
5. 实际应用能创建 OpenGL/EGL 上下文并连续渲染；
6. 性能测试显示该路径比当前基线更稳定，而不只是“插件能加载”。

### 6.2 只能支持“当前运行在软件显示路径”的证据

例如：

- 启动日志明确显示 `linuxfb`；
- 没有可用的 EGL/OpenGL ES 平台插件；
- 应用能够运行，但所有绘制都由 CPU Raster 完成；
- `fbset` 显示的是 Framebuffer，且没有其他可用加速上下文。

即使以上条件都成立，也只说明**当前部署**走软件路径，不能把所有可能的 BSP 配置一概否定。

### 6.3 不足以单独下结论的证据

- 只看到 `/dev/fb0`；
- 只看到 `/dev/dri/card0`；
- 只查到芯片系列；
- 只看到某个 Qt 插件文件；
- 只用 `top` 观察到一个核心占用较高；
- 网上或历史文档中的一句“支持/不支持 GPU”。

## 7. 显示路径候选

以下路径都应在板端确认后选择，不能提前写成已采用或已优化：

| 路径 | 前提 | 需要验证 |
| :--- | :--- | :--- |
| LinuxFB | `/dev/fb*` 和 `linuxfb` 插件可用 | 颜色格式、分辨率、Framebuffer 拷贝、VSync/撕裂 |
| DRM/KMS | DRM 驱动和 connector 正常，Qt KMS 插件可用 | dumb buffer/page flip、格式、权限、与 Qt 版本兼容性 |
| EGLFS/OpenGL ES | 有可用的 GPU/显示驱动和 EGL 上下文 | 插件启动、上下文创建、渲染正确性和端到端延迟 |
| PL 端 2D 加速 | 有明确 RTL/IP、驱动和内存带宽预算 | 数据格式、同步、共享内存和系统集成成本 |
| Web/B-S 分离显示 | 网络链路和 PC 端软件可接受 | 板端采集稳定性、网络延迟、浏览器渲染开销 |

DRM/KMS、PL 加速和 Web 端渲染目前只能作为候选方案；没有同版本对照数据时，不能给出固定收益百分比。

## 8. 板端验收记录

建议为每个候选路径保存一份最小记录：

```text
构建版本:
板卡/BSP:
内核版本:
Qt 版本:
屏幕与分辨率:
QPA:
设备树 GPU/显示节点:
内核驱动日志:
运行命令:
频谱点数 / 输入帧率 / 视图模式:
显示帧率:
频谱离屏渲染耗时:
显示 P95 延迟:
跳过/丢弃帧:
CPU 各核心占用:
画面是否正确:
是否发生撕裂、掉帧或退出:
```

只有在相同构建、相同参数和相同窗口尺寸下完成前后对照，才能把变化归因于某一项优化。

## 9. 相关文档

- [渲染与显示性能资料索引](./README.md)
- [渲染优化方案与验证计划](./RENDER_OPTIMIZATION_PLAN.md)
- [LinuxFB RGB16 零转换说明](../innovations/05_LINUXFB_RGB16_ZERO_CONVERSION.md)
- [项目实现与验证报告](../项目实现与验证报告.md)
- [DMA 数据源接口与 PL/PS 协议](../../design/DMA数据源接口与PL_PS协议确认清单.md)
