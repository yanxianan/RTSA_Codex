MODULE_NAME := rtsa_dma
obj-m := $(MODULE_NAME).o
$(MODULE_NAME)-y := rtsa_dma_driver.o

# 内核源码路径，可通过命令行覆盖，例如：
# make KDIR=/path/to/linux-xlnx ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: default clean
