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

#define FFT_POINTS 1024

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
static void print_ascii_spectrum(const int32_t *data, int num_points) {
    int max_val = -0x7FFFFFFF;
    int min_val = 0x7FFFFFFF;
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

    printf("\n>>> [1024点 FFT 频域峰值分析] <<<\n");
    printf("    正弦波谱线峰值 (Peak) : %d (位于 Bin #%d / 1024)\n", max_val, peak_idx);
    printf("    底噪均值估计 (Noise) : %d\n", min_val);
    printf("    频域 ASCII 预览 (32 柱):\n    ");

    // 将 1024 点压缩为 32 个柱子打印
    int step = num_points / 32;
    for (int b = 0; b < 32; b++) {
        int bin_max = -0x7FFFFFFF;
        for (int i = 0; i < step; i++) {
            int val = data[b * step + i];
            if (val > bin_max) bin_max = val;
        }
        if (bin_max >= max_val * 0.8 && max_val > 1000) {
            printf("^"); // 峰值标记
        } else if (bin_max > (max_val - min_val) * 0.5 + min_val) {
            printf("|");
        } else if (bin_max > (max_val - min_val) * 0.2 + min_val) {
            printf(":");
        } else {
            printf(".");
        }
    }
    printf("\n    0................................1023\n\n");
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

    total_map_size = (size_t)RTSA_RING_BUFFER_COUNT * RTSA_BUFFER_SIZE;
    map_base = mmap(NULL, total_map_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_base == MAP_FAILED) {
        perror("[-] mmap 内存映射失败");
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

        void *slot_ptr = (char *)map_base + (size_t)info.index * RTSA_BUFFER_SIZE;
        const int32_t *fft_data = (const int32_t *)slot_ptr;

        // 抓当前帧最大值位置（在归还槽位前完成读取）
        int cur_peak = 0;
        int cur_max = -0x7FFFFFFF;
        for (int i = 0; i < FFT_POINTS; i++) {
            if (fft_data[i] > cur_max) {
                cur_max = fft_data[i];
                cur_peak = i;
            }
        }

        if (first_frame) {
            first_frame = false;
            last_seq = info.sequence;
            print_ascii_spectrum(fft_data, FFT_POINTS);
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
        // 真实单帧有效载荷：1024 点 x 4 字节 = 4096 字节
        size_t real_frame_bytes = FFT_POINTS * sizeof(int32_t);
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
                   FFT_POINTS,
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
