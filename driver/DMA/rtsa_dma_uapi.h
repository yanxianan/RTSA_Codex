#ifndef _RTSA_DMA_UAPI_H_
#define _RTSA_DMA_UAPI_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define RTSA_RING_BUFFER_COUNT  16          // 环形缓冲数量
#define RTSA_BUFFER_SIZE        (256 * 1024) // 每块 256 KB，足以容纳 64K 点 + 帧头

struct rtsa_buffer_info {
    __u32 index;          // 缓冲槽索引 [0 .. RTSA_RING_BUFFER_COUNT-1]
    __u32 size;           // 实际有效载荷字节数
    __u64 sequence;       // 硬件/驱动帧计数
    __u64 timestamp_ns;   // 完成时间戳
    __u32 status;         // 0: OK, 1: Overflow/Error
};

#define RTSA_IOC_MAGIC       'R'
#define RTSA_IOC_START       _IO(RTSA_IOC_MAGIC, 1)                  // 启动 DMA 接收
#define RTSA_IOC_STOP        _IO(RTSA_IOC_MAGIC, 2)                  // 停止 DMA 接收
#define RTSA_IOC_DEQUEUE     _IOR(RTSA_IOC_MAGIC, 3, struct rtsa_buffer_info) // 取出已完成的缓冲
#define RTSA_IOC_QUEUE       _IOW(RTSA_IOC_MAGIC, 4, __u32)          // 归还缓冲回空闲队列

#endif