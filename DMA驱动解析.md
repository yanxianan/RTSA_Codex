加载新的bit：

fpgautil -b DMA_TOP_0920_1638.bit

开发板UTF-8渲染：

export LC_ALL=C.UTF-8
export LANG=C.UTF-8

使用显示器输出：

env -u DISPLAY QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 ./rtsa_app --source dma

env -u DISPLAY QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 LC_ALL=C.UTF-8 ./rtsa_app --source dma



RTSA编译：

1. CMake 配置（指定 Release 模式并禁用单测）

cmake -S . -B build-aarch64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake" \
  -DRTSA_BUILD_TESTS=OFF

2. 并行编译

cmake --build build-aarch64 --parallel $(nproc)

3. 产物确认

file build-aarch64/src/rtsa_app

正确输出应包含：ARM aarch64, dynamically linked, interpreter /lib/ld-linux-aarch64.so.1

内核编译：

petalinux-build -c rtsa-dma

确认驱动是否安装：

lsmod | grep rtsa_dma

ls -l /dev/rtsa_dma
