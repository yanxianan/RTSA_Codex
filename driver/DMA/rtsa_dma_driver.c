#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/ktime.h>
#include "rtsa_dma_uapi.h"

#define DRIVER_NAME "rtsa_dma"

struct dma_slot {
    void *vaddr;             // CPU 虚拟地址（指向大块内存中的偏移）
    dma_addr_t paddr;        // DMA 物理总线地址（指向大块内存中的偏移）
    struct rtsa_buffer_info info;
};

struct rtsa_dma_dev {
    struct platform_device *pdev;
    struct cdev cdev;
    dev_t dev_node;
    struct class *class;
    struct device *device;

    struct dma_chan *rx_chan;
    struct dma_slot slots[RTSA_RING_BUFFER_COUNT];

    // ===== 修改1：新增大块连续内存的基址记录 =====
    void *dma_base_vaddr;       // 大块内存的 CPU 虚拟基址
    dma_addr_t dma_base_paddr;  // 大块内存的 DMA 物理基址
    size_t dma_total_size;      // 总大小

    // 环形缓冲队列索引与同步
    wait_queue_head_t rx_wait;
    spinlock_t lock;
    __u32 completed_head;
    __u32 completed_tail;
    __u32 completed_queue[RTSA_RING_BUFFER_COUNT];
    
    bool is_running;
    __u64 frame_seq;
};

// DMA 完成中断回调函数（运行在中断底半部/Tasklet 上下文）
static void rtsa_dma_rx_callback(void *param) {
    struct rtsa_dma_dev *rtsa = (struct rtsa_dma_dev *)param;
    unsigned long flags;
    __u32 slot_idx;

    spin_lock_irqsave(&rtsa->lock, flags);
    if (!rtsa->is_running) {
        spin_unlock_irqrestore(&rtsa->lock, flags);
        return;
    }

    slot_idx = rtsa->completed_queue[rtsa->completed_head];
    rtsa->slots[slot_idx].info.sequence = ++rtsa->frame_seq;
    rtsa->slots[slot_idx].info.timestamp_ns = ktime_get_ns();
    rtsa->slots[slot_idx].info.size = RTSA_BUFFER_SIZE;
    rtsa->slots[slot_idx].info.status = 0;

    rtsa->completed_head = (rtsa->completed_head + 1) % RTSA_RING_BUFFER_COUNT;
    spin_unlock_irqrestore(&rtsa->lock, flags);

    wake_up_interruptible(&rtsa->rx_wait);
}

// 提交一个 DMA 接收描述符到 DMAEngine
static int rtsa_submit_rx_slot(struct rtsa_dma_dev *rtsa, __u32 slot_idx) {
    struct dma_async_tx_descriptor *desc;
    dma_cookie_t cookie;

    desc = dmaengine_prep_slave_single(
        rtsa->rx_chan,
        rtsa->slots[slot_idx].paddr,
        RTSA_BUFFER_SIZE,
        DMA_DEV_TO_MEM,
        DMA_PREP_INTERRUPT | DMA_CTRL_ACK
    );

    if (!desc) {
        dev_err(&rtsa->pdev->dev, "Failed to prepare DMA descriptor\n");
        return -ENOMEM;
    }

    desc->callback = rtsa_dma_rx_callback;
    desc->callback_param = rtsa;

    cookie = dmaengine_submit(desc);
    if (dma_submit_error(cookie)) {
        dev_err(&rtsa->pdev->dev, "DMA submit error\n");
        return -EIO;
    }

    dma_async_issue_pending(rtsa->rx_chan);
    return 0;
}

// ===== 修改2：安全的 mmap，基于大块连续内存基址 =====
static int rtsa_dma_mmap(struct file *filp, struct vm_area_struct *vma) {
    struct rtsa_dma_dev *rtsa = filp->private_data;
    size_t map_size = vma->vm_end - vma->vm_start;

    // 安全检查：必须从文件头开始映射（避免随机偏移）
    if (vma->vm_pgoff != 0) {
        pr_err("rtsa_dma: mmap offset must be 0\n");
        return -EINVAL;
    }

    if (map_size > rtsa->dma_total_size) {
        pr_err("rtsa_dma: mmap size %zu exceeds total %zu\n", 
               map_size, rtsa->dma_total_size);
        return -EINVAL;
    }

    // 直接映射大块物理连续内存
    return dma_mmap_coherent(&rtsa->pdev->dev, vma,
                             rtsa->dma_base_vaddr,
                             rtsa->dma_base_paddr,
                             map_size);
}

static __poll_t rtsa_dma_poll(struct file *filp, struct poll_table_struct *wait) {
    struct rtsa_dma_dev *rtsa = filp->private_data;
    __poll_t mask = 0;
    unsigned long flags;

    poll_wait(filp, &rtsa->rx_wait, wait);

    spin_lock_irqsave(&rtsa->lock, flags);
    if (rtsa->completed_head != rtsa->completed_tail) {
        mask |= EPOLLIN | EPOLLRDNORM;
    }
    spin_unlock_irqrestore(&rtsa->lock, flags);

    return mask;
}

static long rtsa_dma_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct rtsa_dma_dev *rtsa = filp->private_data;
    unsigned long flags;
    int ret = 0;

    switch (cmd) {
    case RTSA_IOC_START: {
        // ===== 修改3：在自旋锁外提交描述符，防止在锁内睡眠 =====
        spin_lock_irqsave(&rtsa->lock, flags);
        if (!rtsa->is_running) {
            rtsa->is_running = true;
            rtsa->completed_head = 0;
            rtsa->completed_tail = 0;
            // 预填完成队列的索引（在锁内完成，保证回调看到有效值）
            for (int i = 0; i < RTSA_RING_BUFFER_COUNT / 2; i++) {
                rtsa->completed_queue[i] = i;
            }
        }
        spin_unlock_irqrestore(&rtsa->lock, flags);

        // 在锁外提交硬件描述符（因为 dmaengine_prep 可能睡眠）
        if (rtsa->is_running) {
            for (int i = 0; i < RTSA_RING_BUFFER_COUNT / 2; i++) {
                ret = rtsa_submit_rx_slot(rtsa, i);
                if (ret) {
                    dev_err(&rtsa->pdev->dev, "Failed to submit initial slot %d\n", i);
                    // 如果提交失败，停止硬件标记，防止脏状态
                    spin_lock_irqsave(&rtsa->lock, flags);
                    rtsa->is_running = false;
                    spin_unlock_irqrestore(&rtsa->lock, flags);
                    return ret;
                }
            }
        }
        break;
    }

    case RTSA_IOC_STOP:
        spin_lock_irqsave(&rtsa->lock, flags);
        rtsa->is_running = false;
        spin_unlock_irqrestore(&rtsa->lock, flags);
        dmaengine_terminate_sync(rtsa->rx_chan);
        wake_up_interruptible(&rtsa->rx_wait);
        break;

    case RTSA_IOC_DEQUEUE: {
        struct rtsa_buffer_info info;
        spin_lock_irqsave(&rtsa->lock, flags);
        if (rtsa->completed_head == rtsa->completed_tail) {
            spin_unlock_irqrestore(&rtsa->lock, flags);
            return -EAGAIN;
        }
        __u32 slot = rtsa->completed_queue[rtsa->completed_tail];
        info = rtsa->slots[slot].info;
        info.index = slot;
        rtsa->completed_tail = (rtsa->completed_tail + 1) % RTSA_RING_BUFFER_COUNT;
        spin_unlock_irqrestore(&rtsa->lock, flags);

        if (copy_to_user((void __user *)arg, &info, sizeof(info)))
            return -EFAULT;
        break;
    }

    case RTSA_IOC_QUEUE: {
        __u32 slot_idx;
        if (copy_from_user(&slot_idx, (void __user *)arg, sizeof(slot_idx)))
            return -EFAULT;
        if (slot_idx >= RTSA_RING_BUFFER_COUNT)
            return -EINVAL;

        ret = rtsa_submit_rx_slot(rtsa, slot_idx);
        break;
    }

    default:
        ret = -ENOTTY;
    }

    return ret;
}

static int rtsa_dma_open(struct inode *inode, struct file *filp) {
    struct rtsa_dma_dev *rtsa = container_of(inode->i_cdev, struct rtsa_dma_dev, cdev);
    filp->private_data = rtsa;
    return 0;
}

static const struct file_operations rtsa_dma_fops = {
    .owner          = THIS_MODULE,
    .open           = rtsa_dma_open,
    .mmap           = rtsa_dma_mmap,
    .poll           = rtsa_dma_poll,
    .unlocked_ioctl = rtsa_dma_ioctl,
};

// ===== 修改4：安全的 probe（一次申请 + 错误回滚） =====
static int rtsa_dma_probe(struct platform_device *pdev) {
    struct rtsa_dma_dev *rtsa;
    int ret, i;

    rtsa = devm_kzalloc(&pdev->dev, sizeof(*rtsa), GFP_KERNEL);
    if (!rtsa)
        return -ENOMEM;

    rtsa->pdev = pdev;
    spin_lock_init(&rtsa->lock);
    init_waitqueue_head(&rtsa->rx_wait);

    // 1. 获取 DMA 通道
    rtsa->rx_chan = dma_request_chan(&pdev->dev, "rx_chan");
    if (IS_ERR(rtsa->rx_chan)) {
        dev_err(&pdev->dev, "Failed to request DMA channel\n");
        return PTR_ERR(rtsa->rx_chan);
    }

    // ===== 核心修改：一次性申请所有缓冲区（物理连续） =====
    rtsa->dma_total_size = RTSA_RING_BUFFER_COUNT * RTSA_BUFFER_SIZE;
    rtsa->dma_base_vaddr = dma_alloc_coherent(
        &pdev->dev,
        rtsa->dma_total_size,
        &rtsa->dma_base_paddr,
        GFP_KERNEL
    );
    if (!rtsa->dma_base_vaddr) {
        dev_err(&pdev->dev, "Failed to allocate contiguous DMA memory\n");
        ret = -ENOMEM;
        goto err_free_chan;
    }

    // 2. 将大块内存切分成虚拟环，每个 slot 指向对应偏移
    for (i = 0; i < RTSA_RING_BUFFER_COUNT; i++) {
        rtsa->slots[i].vaddr = rtsa->dma_base_vaddr + i * RTSA_BUFFER_SIZE;
        rtsa->slots[i].paddr = rtsa->dma_base_paddr + i * RTSA_BUFFER_SIZE;
        memset(&rtsa->slots[i].info, 0, sizeof(rtsa->slots[i].info));
    }

    // 3. 注册字符设备
    ret = alloc_chrdev_region(&rtsa->dev_node, 0, 1, DRIVER_NAME);
    if (ret) {
        dev_err(&pdev->dev, "Failed to alloc chrdev region\n");
        goto err_free_dma;
    }
    cdev_init(&rtsa->cdev, &rtsa_dma_fops);
    ret = cdev_add(&rtsa->cdev, rtsa->dev_node, 1);
    if (ret) {
        dev_err(&pdev->dev, "Failed to add cdev\n");
        goto err_unregister_region;
    }

    rtsa->class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(rtsa->class)) {
        ret = PTR_ERR(rtsa->class);
        goto err_cdev_del;
    }

    rtsa->device = device_create(rtsa->class, NULL, rtsa->dev_node, NULL, DRIVER_NAME);
    if (IS_ERR(rtsa->device)) {
        ret = PTR_ERR(rtsa->device);
        goto err_class_destroy;
    }

    platform_set_drvdata(pdev, rtsa);
    dev_info(&pdev->dev, "RTSA DMA probed, total size: %zu bytes\n", rtsa->dma_total_size);
    return 0;

// 错误回滚标签
err_class_destroy:
    class_destroy(rtsa->class);
err_cdev_del:
    cdev_del(&rtsa->cdev);
err_unregister_region:
    unregister_chrdev_region(rtsa->dev_node, 1);
err_free_dma:
    dma_free_coherent(&pdev->dev, rtsa->dma_total_size,
                      rtsa->dma_base_vaddr, rtsa->dma_base_paddr);
err_free_chan:
    dma_release_channel(rtsa->rx_chan);
    return ret;
}

// ===== 修改5：安全的 remove（一次性释放 + 停止硬件） =====
static int rtsa_dma_remove(struct platform_device *pdev) {
    struct rtsa_dma_dev *rtsa = platform_get_drvdata(pdev);

    // 1. 先停止硬件，防止还在传输
    if (rtsa->is_running) {
        dmaengine_terminate_sync(rtsa->rx_chan);
    }

    // 2. 销毁设备节点
    device_destroy(rtsa->class, rtsa->dev_node);
    class_destroy(rtsa->class);
    cdev_del(&rtsa->cdev);
    unregister_chrdev_region(rtsa->dev_node, 1);

    // 3. 一次性释放大块连续内存
    if (rtsa->dma_base_vaddr) {
        dma_free_coherent(&pdev->dev, rtsa->dma_total_size,
                          rtsa->dma_base_vaddr, rtsa->dma_base_paddr);
    }

    // 4. 释放 DMA 通道
    if (rtsa->rx_chan) {
        dma_release_channel(rtsa->rx_chan);
    }

    return 0;
}

static const struct of_device_id rtsa_dma_of_match[] = {
    { .compatible = "rtsa,spectrum-dma" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtsa_dma_of_match);

static struct platform_driver rtsa_dma_driver = {
    .probe  = rtsa_dma_probe,
    .remove = rtsa_dma_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = rtsa_dma_of_match,
    },
};
module_platform_driver(rtsa_dma_driver);

MODULE_AUTHOR("RTSA Team");
MODULE_DESCRIPTION("High-Performance Spectrum AXI DMA Driver (Fixed Version)");
MODULE_LICENSE("GPL");