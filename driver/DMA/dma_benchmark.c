#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <math.h>
#include <errno.h>
#include "rtsa_dma_uapi.h"

#define DEFAULT_BIN_COUNT 1024
#define SPECTRUM_HEADER_BYTES 64

static volatile bool g_running = true;

static void sig_handler(int signo) {
    (void)signo;
    g_running = false;
}

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// 打印简易控制台 ASCII 频谱柱状图
static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24);
}

static uint64_t read_le64(const uint8_t *data) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static void print_ascii_spectrum(const int16_t *data, int num_points,
                                 double offset_db) {
    int max_val = -32768;
    int min_val = 32767;
    int peak_idx = 0;

    for (int i = 0; i < num_points; i++) {
        if (data[i] > max_val) {
            max_val = data[i];
            peak_idx = i;
        }
        if (data[i] < min_val) {
            min_val = data[i];
        }
    }

    printf("\n>>> [%d点 FFT 频域峰值分析] <<<\n", num_points);
    printf("    谱线峰值 (Peak) : %.2f dBFS (位于 Bin #%d / %d)\n",
           max_val * 0.01 + offset_db, peak_idx, num_points);
    printf("    底噪估计 (Noise) : %.2f dBFS\n", min_val * 0.01 + offset_db);
    printf("    频域 ASCII 预览 (32 柱):\n    ");

    // 将 1024 点压缩为 32 个柱子打印
    int step = num_points / 32;
    for (int b = 0; b < 32; b++) {
        int bin_max = -0x7FFFFFFF;
        for (int i = 0; i < step; i++) {
            int val = data[b * step + i];
            if (val > bin_max) bin_max = val;
        }
        if (bin_max >= max_val - 300) {
            printf("^"); // 峰值标记
        } else if (bin_max > (max_val - min_val) / 2 + min_val) {
            printf("|");
        } else if (bin_max > (max_val - min_val) / 5 + min_val) {
            printf(":");
        } else {
            printf(".");
        }
    }
    printf("\n    0................................%d\n\n", num_points - 1);
}

int main(int argc, char *argv[]) {
    const char *dev_path = "/dev/rtsa_dma";
    int timeout_ms = 3000;
    int fd;
    void *map_base;
    size_t total_map_size;
    int ret;

    if (argc > 1) {
        timeout_ms = atoi(argv[1]);
        if (timeout_ms <= 0) timeout_ms = 3000;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("====================================================\n");
    printf("       RTSA AXI DMA 连通性与频谱峰值分析工具        \n");
    printf("====================================================\n");

    fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("[-] 打开设备失败");
        return -1;
    }

    total_map_size = (size_t)RTSA_RING_BUFFER_COUNT * RTSA_SLOT_SIZE;
    map_base = mmap(NULL, total_map_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_base == MAP_FAILED) {
        perror("[-] mmap 内存映射失败");
        close(fd);
        return -1;
    }

    __u32 frame_size = SPECTRUM_HEADER_BYTES + DEFAULT_BIN_COUNT * 2;
    if (ioctl(fd, RTSA_IOC_SET_FRAME_SIZE, &frame_size) < 0) {
        perror("[-] 设置帧长失败");
        munmap(map_base, total_map_size);
        close(fd);
        return -1;
    }

    if (ioctl(fd, RTSA_IOC_START) < 0) {
        perror("[-] 启动 DMA 失败");
        munmap(map_base, total_map_size);
        close(fd);
        return -1;
    }

    printf("[+] DMA 启动成功！正在捕获 DDS 频谱数据 ...\n");

    uint64_t total_frames = 0;
    uint64_t total_bytes = 0;
    uint64_t last_seq = 0;
    uint64_t dropped_frames = 0;
    uint64_t interval_frames = 0;
    uint64_t start_time = get_time_ns();
    uint64_t last_report_time = start_time;
    bool first_frame = true;

    while (g_running) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        ret = poll(&pfd, 1, timeout_ms);

        if (ret <= 0) {
            if (ret < 0 && errno == EINTR) break;
            printf("[!] 等待超时！\n");
            continue;
        }

        struct rtsa_buffer_info info;
        if (ioctl(fd, RTSA_IOC_DEQUEUE, &info) < 0) {
            if (errno == EAGAIN) continue;
            break;
        }

        if (info.index >= RTSA_RING_BUFFER_COUNT) {
            fprintf(stderr, "[-] 槽号越界: %u >= %u\n", info.index, RTSA_RING_BUFFER_COUNT);
            break;
        }

        uint8_t *slot_ptr = (uint8_t *)map_base + (size_t)info.index * RTSA_SLOT_SIZE;
        if (info.size < SPECTRUM_HEADER_BYTES) {
            fprintf(stderr, "[-] 短帧: %u bytes\n", info.size);
            ioctl(fd, RTSA_IOC_QUEUE, &info.index);
            continue;
        }

        uint32_t magic = read_le32(slot_ptr + 0x00);
        uint16_t version = read_le16(slot_ptr + 0x04);
        uint32_t frame_bytes = read_le32(slot_ptr + 0x08);
        uint32_t bin_count = read_le32(slot_ptr + 0x30);
        uint8_t trace_mode = slot_ptr[0x34];
        uint16_t accumulation_frames = read_le16(slot_ptr + 0x35);
        uint64_t span_hz = read_le64(slot_ptr + 0x28);
        int32_t calibration_q16_16 = (int32_t)read_le32(slot_ptr + 0x38);
        uint8_t data_format = slot_ptr[0x3C];

        if (magic != 0x52545341U || version != 1U || data_format != 0U
            || bin_count == 0U || bin_count > 65536U
            || frame_bytes != SPECTRUM_HEADER_BYTES + bin_count * 2U
            || info.size < frame_bytes) {
            fprintf(stderr, "[-] 帧头错误: magic=%08x version=%u format=%u bins=%u frame=%u size=%u\n",
                    magic, version, data_format, bin_count, frame_bytes, info.size);
            ioctl(fd, RTSA_IOC_QUEUE, &info.index);
            continue;
        }

        const int16_t *fft_data = (const int16_t *)(slot_ptr + SPECTRUM_HEADER_BYTES);

        // 抓当前帧最大值位置（在归还槽位前完成读取）
        int cur_peak = 0;
        int cur_max = -0x7FFFFFFF;
        for (int i = 0; i < (int)bin_count; i++) {
            if (fft_data[i] > cur_max) {
                cur_max = fft_data[i];
                cur_peak = i;
            }
        }

        if (first_frame) {
            first_frame = false;
            last_seq = info.sequence;
            printf("[+] 帧头: magic=RTSA version=%u frame=%u bytes bins=%u trace=%u accum=%u span=%.3f MHz\n",
                   version, frame_bytes, bin_count, trace_mode,
                   accumulation_frames, (double)span_hz / 1.0e6);
            print_ascii_spectrum(fft_data, bin_count,
                                 calibration_q16_16 / 65536.0);
            printf("--------------------------------------------------------------------\n");
            printf("%-10s %-12s %-12s %-10s %-12s %-10s\n", 
                   "时间", "实时帧率", "真实带宽", "点数/帧", "谱线峰值点", "丢帧/溢出");
            printf("--------------------------------------------------------------------\n");
        } else {
            if (info.sequence > last_seq + 1) {
                dropped_frames += (info.sequence - last_seq - 1);
            }
            last_seq = info.sequence;
        }

        if (info.status != 0) dropped_frames++;

        total_frames++;
        size_t real_frame_bytes = frame_bytes;
        total_bytes += real_frame_bytes;
        interval_frames++;

        // 还回槽位给驱动
        ioctl(fd, RTSA_IOC_QUEUE, &info.index);

        // 每秒统计输出
        uint64_t now = get_time_ns();
        uint64_t diff_ns = now - last_report_time;
        if (diff_ns >= 1000000000ULL) {
            double elapsed_sec = (double)diff_ns / 1.0e9;
            double fps = (double)interval_frames / elapsed_sec;
            double mbps = ((double)(interval_frames * real_frame_bytes) / (1024.0 * 1024.0)) / elapsed_sec;

            time_t rawtime;
            struct tm *timeinfo;
            char time_str[16];
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);

            printf("%-10s %-10.1f fps %-9.2f MB/s %-10d Bin #%-7d %-10llu\n",
                   time_str,
                   fps,
                   mbps,
                   (int)bin_count,
                   cur_peak,
                   (unsigned long long)dropped_frames);
            fflush(stdout);

            interval_frames = 0;
            last_report_time = now;
        }
    }

    printf("\n[*] 正在停止 DMA ...\n");
    ioctl(fd, RTSA_IOC_STOP);
    munmap(map_base, total_map_size);
    close(fd);

    uint64_t total_time_ns = get_time_ns() - start_time;
    double total_sec = (double)total_time_ns / 1.0e9;
    printf("\n====================================================\n");
    printf("                  测试总结报告                      \n");
    printf("====================================================\n");
    printf("  运行总时长   : %.2f 秒\n", total_sec);
    printf("  接收总帧数   : %llu 帧\n", (unsigned long long)total_frames);
    printf("  真实总数据量 : %.2f MB\n", (double)total_bytes / (1024.0 * 1024.0));
    if (total_sec > 0.0) {
        printf("  平均帧率     : %.2f fps\n", (double)total_frames / total_sec);
        printf("  真实物理带宽 : %.2f MB/s (%.2f Gbps)\n", 
               ((double)total_bytes / (1024.0 * 1024.0)) / total_sec,
               (((double)total_bytes * 8.0) / 1.0e9) / total_sec);
    }
    printf("  丢帧/溢出数  : %llu 帧\n", (unsigned long long)dropped_frames);
    printf("====================================================\n");

    return 0;
}
