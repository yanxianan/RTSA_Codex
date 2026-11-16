#!/bin/sh
# Load a full PL bitstream and re-probe the RTSA DMA driver.
#
# Why this is required:
#   The ZynqMP boot image may configure the PL after Linux has already probed
#   the AXI DMA driver. Reconfiguring the PL clears AXI DMA DMACR, including
#   the IOC/DLY/ERR interrupt enables. The dmaengine driver does not restore
#   those bits on RTSA_IOC_START, so a first frame can complete with IOC=1 but
#   no CPU interrupt, and the descriptor ring stops after one transfer.
#
# Usage:
#   sh load_bitstream_and_reload_dma.sh [bitstream]
#
set -eu

BITSTREAM="${1:-/home/root/DMA_TOP_20261106a.bit}"
MODULE="${RTSA_DMA_MODULE:-/home/root/rtsa_dma.ko}"

if [ ! -f "$BITSTREAM" ]; then
    echo "bitstream not found: $BITSTREAM" >&2
    exit 1
fi

if [ ! -f "$MODULE" ]; then
    echo "driver module not found: $MODULE" >&2
    exit 1
fi

echo "[1/3] loading bitstream: $BITSTREAM"
fpgautil -b "$BITSTREAM" -f Full -n full

echo "[2/3] re-probing RTSA DMA driver"
pkill rtsa_app 2>/dev/null || true
sleep 1
rmmod rtsa_dma 2>/dev/null || true
insmod "$MODULE"

echo "[3/3] checking DMA interrupt enables"
DMACR="$(devmem 0xA0080030 32 2>/dev/null || true)"
echo "S2MM_DMACR=$DMACR"
case "$DMACR" in
    0x*[7-9a-fA-F]*) echo "IRQ enable bits look present" ;;
    *) echo "warning: expected IOC/DLY/ERR bits (0x7000) in DMACR" >&2 ;;
esac
