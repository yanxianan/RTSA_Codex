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

#define RTSA_UAPI_VERSION       3

#define RTSA_RING_BUFFER_COUNT  128
#define RTSA_SLOT_SIZE          (132 * 1024)
#define RTSA_MAX_FRAME_SIZE     (131136)     // 64 B header + 65536 × int16
#define RTSA_BUFFER_SIZE        RTSA_SLOT_SIZE

struct rtsa_buffer_info {
    __u32 index;          // 缓冲槽索引 [0 .. RTSA_RING_BUFFER_COUNT-1]
    __u32 size;           // 实际 DMA 完成字节数，至少应 >= frame_bytes
    __u64 sequence;       // 硬件/驱动帧计数
    __u64 timestamp_ns;   // 完成时间戳
    __u32 status;         // bit0: overflow, bit1: short frame, bit2: dma error
    __u32 reserved;       // 显式 64 位对齐填充，杜绝内核栈信息泄露与跨体系 ABI 歧义
};

struct rtsa_stats {
    __u64 completed_frames;
    __u64 completed_bytes;
    __u64 queue_overflows;
    __u64 short_frames;
    __u64 dma_errors;
    __u32 frame_size;
    __u32 ring_count;
};

#define RTSA_IOC_MAGIC       'R'
#define RTSA_IOC_START       _IO(RTSA_IOC_MAGIC, 1)                           // 启动 DMA 接收
#define RTSA_IOC_STOP        _IO(RTSA_IOC_MAGIC, 2)                           // 停止 DMA 接收
#define RTSA_IOC_DEQUEUE     _IOR(RTSA_IOC_MAGIC, 3, struct rtsa_buffer_info) // 取出已完成的缓冲
#define RTSA_IOC_QUEUE       _IOW(RTSA_IOC_MAGIC, 4, __u32)                   // 归还缓冲回空闲队列
#define RTSA_IOC_SET_FRAME_SIZE _IOW(RTSA_IOC_MAGIC, 5, __u32)                // 停止状态下设置帧长
#define RTSA_IOC_GET_STATS   _IOR(RTSA_IOC_MAGIC, 6, struct rtsa_stats)       // 查询统计
#define RTSA_IOC_GET_VERSION _IOR(RTSA_IOC_MAGIC, 7, __u32)                   // 查询 UAPI 版本

#endif
