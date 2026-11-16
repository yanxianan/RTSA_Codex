# DMA 驱动构建与板端部署记录

| 项目 | 内容 |
|---|---|
| 文档版本 | V0.3 |
| 文档状态 | 当前驱动/UAPI 说明 + 现场待确认 |
| 最近修订 | 2026-11-06 |
| 适用平台 | PetaLinux 2023.2 / ZU47DR AArch64 |
| 结论边界 | 仓库已有驱动基线，不代表当前版本已完成板端验收 |

## 1. 文件与职责

| 文件 | 用途 |
|---|---|
| `driver/DMA/rtsa_dma_driver.c` | dmaengine 字符设备驱动 |
| `driver/DMA/rtsa_dma_uapi.h` | 用户态与内核共享的 ioctl 和槽位结构 |
| `driver/DMA/system-user.dtsi` | 设备树示例 |
| `driver/DMA/Makefile` | 外部内核模块构建 |
| `driver/DMA/dma_benchmark.c` | DMA 连通性、帧率和峰值检查工具 |

驱动 compatible 为 `rtsa,spectrum-dma`，请求 `rx_chan` dmaengine 通道，使用 coherent DMA 环，并向用户态提供 `/dev/rtsa_dma`。

## 2. UAPI 基线

- UAPI 版本：`RTSA_UAPI_VERSION = 2`；
- 槽位数：128；
- 单槽容量：`RTSA_SLOT_SIZE = 132 KiB`；
- 总映射：`128 × 132 KiB ≈ 16.5 MiB`；
- 单帧长度：`frame_size`，默认 `RTSA_MAX_FRAME_SIZE = 131136`，当前 1024 点频谱帧实际使用 `2112` B；
- 操作：`RTSA_IOC_START`、`RTSA_IOC_STOP`、`RTSA_IOC_DEQUEUE`、`RTSA_IOC_QUEUE`、`RTSA_IOC_SET_FRAME_SIZE`、`RTSA_IOC_GET_STATS`；
- 完成等待：`poll`；
- 完成信息：槽索引、有效字节数、序号、时间戳和状态；
- 状态位：bit0 队列溢出、bit1 短帧、bit2 DMA 错误；
- 统计：`struct rtsa_stats`，包含完成帧/字节、队列溢出、短帧、DMA 错误、当前帧长和环槽数。

用户态必须使用与驱动同一版本的 `rtsa_dma_uapi.h`。换驱动后应重新编译应用，不能把旧 `rtsa_dma.ko` 与新应用混用。

## 3. 设备树

示例节点位于 `system-user.dtsi`：

```dts
&axi_bram_ctrl_0 {
    compatible = "generic-uio";
    linux,uio-name = "rtsa_regs";
    status = "okay";
};

&amba_pl {
    rtsa_dma_node {
        compatible = "rtsa,spectrum-dma";
        dmas = <&axi_dma_0 0>;
        dma-names = "rx_chan";
        status = "okay";
    };
};

&axi_dma_0 {
    status = "okay";
};
```

`linux,uio-name = "rtsa_regs"` 是当前 PS 软件查找 PL 寄存器节点的依据；缺少它时，软件无法打开 `rtsa_regs`，参数控制会失效。注意内核 `uio_pdrv_genirq` 读取的是带 `linux,` 前缀的属性名，写成 `uio-name` 只会得到默认节点名。示例文件还保留了显示器、CMA 和其他工程相关节点。集成到实际 PetaLinux 工程时，应按当前硬件设计逐项核对，不要直接覆盖已有 `system-user.dtsi`。

## 4. 编译驱动

### 4.1 在 Linux 内核源码树外编译

```sh
make -C driver/DMA \
  KDIR=/path/to/petalinux/components/linux-xlnx \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu-
```

生成模块应为 `rtsa_dma.ko`。具体 `KDIR`、交叉工具链前缀和内核配置必须与目标 PetaLinux 工程一致。

### 4.2 通过 PetaLinux recipe 编译

如果工程已经为驱动建立 recipe，可执行：

```sh
petalinux-build -c rtsa-dma
```

recipe 名称、安装路径和模块自动加载策略属于现场工程配置，当前仓库未提供完整 recipe，因此该命令不能单独证明驱动已集成。

## 5. 加载与设备检查

加载前应先确认 bit 文件、设备树和 AXI DMA 实例与当前硬件匹配。历史命令记录中曾使用：

```sh
fpgautil -b DMA_TOP_0920_1638.bit
```

该文件名仅是记录，不应写入自动部署脚本。

加载模块：

```sh
insmod rtsa_dma.ko
lsmod | grep rtsa_dma
ls -l /dev/rtsa_dma
```

检查内核日志：

```sh
dmesg | tail -n 100
```

应重点确认：

- 驱动 probe 成功；
- `rx_chan` 获取成功；
- coherent 约 16.5 MiB（128 × 132 KiB）分配成功；
- 字符设备创建成功；
- 没有 DMA 或设备树资源错误。

## 6. 权限

应用需要读写 `/dev/rtsa_dma` 并执行 `mmap`。开发阶段可使用 udev 规则或明确组权限；生产环境不建议长期使用 `chmod 777`。

临时验证可执行：

```sh
sudo chmod 660 /dev/rtsa_dma
sudo chgrp <rtsa-group> /dev/rtsa_dma
```

正式规则应由系统集成负责，并限制多进程同时访问。

## 7. DMA 连通性工具

编译：

```sh
aarch64-linux-gnu-gcc -O2 \
  -I driver/DMA \
  driver/DMA/dma_benchmark.c \
  -o dma_benchmark
```

运行：

```sh
./dma_benchmark 3000
```

参数为单次 `poll` 超时毫秒数，省略时默认 3000 ms。工具会打印帧率、数据量、峰值 bin 和序号缺口等信息。

工具仍按固定 1,024 点预览，并以收到的槽数量估算缺口。用于正式验收前，应让它读取协议中的 bin count，并分别统计 PL、驱动、用户态和显示层错误。

## 8. 应用交叉编译

在已经加载 PetaLinux Qt SDK 环境后配置：

```sh
cmake -S . -B build-aarch64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake" \
  -DRTSA_BUILD_TESTS=OFF

cmake --build build-aarch64 --parallel "$(nproc)"
file build-aarch64/src/rtsa_app
```

期望文件类型为 AArch64 动态可执行文件，但只有目标 rootfs 中具备对应 Qt 库、QPA 插件和字体时才能启动。

## 9. linuxfb 启动记录

只使用 linuxfb 并排除已有 DISPLAY：

```sh
env -u DISPLAY \
  QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 \
  LC_ALL=C.UTF-8 \
  LANG=C.UTF-8 \
  ./rtsa_app --source dma
```

这些命令属于现场记录，尚未形成当前版本的板端验收证据。若系统使用 XCB、Wayland 或其他 QPA，应替换环境变量并按对应平台排查。

## 10. 推荐 bring-up 顺序

1. 在不加载应用的情况下确认 bit、AXI DMA 和驱动 probe；
2. 使用固定 ramp 图样验证槽序号、`info.size` 和字节序；
3. 使用单峰和双峰图样验证频点顺序和峰值位置；
4. 用 `dma_benchmark` 观察低速连续帧和序号缺口；
5. 启动 `rtsa_app --source dma`，确认用户态转换和显示；
6. 验证单帧、暂停、继续、停止和应用重启；
7. 逐级提高帧率，记录 PL、DMA、用户态和 GUI 各层统计；
8. 注入超时、断流、驱动重载和 bit 重载，验证恢复；
9. 接真实 DDC/FFT，再执行频率、幅度、RBW 和长稳验收。

## 11. 故障恢复

建议保留以下恢复操作，但每次执行后都要重新确认设备节点和资源状态：

```sh
rmmod rtsa_dma
insmod rtsa_dma.ko
ls -l /dev/rtsa_dma
dmesg | tail -n 100
```

若需要重新加载 PL（当前宽带版本）：

```sh
fpgautil -b DMA_2026_1012/DMA_TOP_20261106a.bit
```

不要把 `rmmod` 当作已解决故障的证明。必须记录失败时的槽状态、驱动日志、应用日志和能否重复恢复。

## 12. 待确认项

- 实际 PetaLinux 内核版本与 dmaengine API；
- AXI DMA IP 模式、地址和中断；
- 板端 `TLAST` 与 `frame_size=2112` 的实际完成行为；
- 312.5 MHz 宽带模式下的持续帧率、短帧和队列溢出统计；
- 多进程访问策略；
- 目标屏幕 QPA 和分辨率；
- Qt 5.15.x SDK 与 rootfs 依赖；
- 名义/峰值帧率、延迟、丢帧和 24 小时长稳指标。

在以上项目完成前，只能说明驱动和部署命令已经存在，不能说明 ZU47DR 板端 DMA 链路验收完成。
