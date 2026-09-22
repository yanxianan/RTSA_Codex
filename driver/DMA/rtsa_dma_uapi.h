#ifndef _RTSA_DMA_UAPI_H_
#define _RTSA_DMA_UAPI_H_

#ifdef __linux__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <cstdint>
using __u32 = uint32_t;
using __u64 = uint64_t;
#define _IO(type,nr)        ((type) << 8 | (nr))
#define _IOR(type,nr,size)  ((type) << 8 | (nr))
#define _IOW(type,nr,size)  ((type) << 8 | (nr))
#endif

#define RTSA_RING_BUFFER_COUNT  128         // 环形缓冲数量：扩充至 128（1000 fps 下提供 128ms 缓冲，彻底杜绝丢帧）
#define RTSA_BUFFER_SIZE        (256 * 1024) // 每块 256 KB，足以容纳 64K 点 + 帧头

struct rtsa_buffer_info {
    __u32 index;          // 缓冲槽索引 [0 .. RTSA_RING_BUFFER_COUNT-1]
    __u32 size;           // 实际有效载荷字节数
    __u64 sequence;       // 硬件/驱动帧计数
    __u64 timestamp_ns;   // 完成时间戳
    __u32 status;         // 0: OK, 1: Overflow/Error
    __u32 reserved;       // 显式 64 位对齐填充，杜绝内核栈信息泄露与跨体系 ABI 歧义
};

#define RTSA_IOC_MAGIC       'R'
#define RTSA_IOC_START       _IO(RTSA_IOC_MAGIC, 1)                           // 启动 DMA 接收
#define RTSA_IOC_STOP        _IO(RTSA_IOC_MAGIC, 2)                           // 停止 DMA 接收
#define RTSA_IOC_DEQUEUE     _IOR(RTSA_IOC_MAGIC, 3, struct rtsa_buffer_info) // 取出已完成的缓冲
#define RTSA_IOC_QUEUE       _IOW(RTSA_IOC_MAGIC, 4, __u32)                   // 归还缓冲回空闲队列

#endif