#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/of.h>
#include "rtsa_dma_uapi.h"

#define DRIVER_NAME "rtsa_dma"

enum slot_state {
    SLOT_STATE_FREE = 0,
    SLOT_STATE_QUEUED,
    SLOT_STATE_COMPLETED,
};

struct rtsa_dma_dev;

struct dma_slot {
    struct rtsa_dma_dev *rtsa;
    __u32 index;
    void *vaddr;             // CPU 虚拟地址（指向大块内存中的偏移）
    dma_addr_t paddr;        // DMA 物理总线地址（指向大块内存中的偏移）
    struct rtsa_buffer_info info;
    enum slot_state state;
    dma_cookie_t cookie;
};

struct rtsa_dma_dev {
    struct platform_device *pdev;
    struct cdev cdev;
    dev_t dev_node;
    struct class *class;
    struct device *device;

    struct dma_chan *rx_chan;
    struct dma_slot slots[RTSA_RING_BUFFER_COUNT];

    void *dma_base_vaddr;       // 大块连续内存的 CPU 虚拟基址
    dma_addr_t dma_base_paddr;  // 大块连续内存的 DMA 物理基址
    size_t dma_total_size;      // 总大小

    // 环形缓冲队列索引与同步
    wait_queue_head_t rx_wait;
    spinlock_t lock;
    __u32 completed_head;
    __u32 completed_tail;
    __u32 completed_queue[RTSA_RING_BUFFER_COUNT];

    unsigned long is_opened;    // 单设备排他性打开标志
    bool is_running;
    __u64 frame_seq;
};

static int rtsa_submit_rx_slot(struct rtsa_dma_dev *rtsa, __u32 slot_idx);

// DMA 完成中断回调函数（运行在中断底半部/Tasklet 上下文）
static void rtsa_dma_rx_callback(void *param) {
    struct dma_slot *slot = (struct dma_slot *)param;
    struct rtsa_dma_dev *rtsa = slot->rtsa;
    unsigned long flags;
    __u32 next_head;
    struct dma_tx_state state;
    enum dma_status dma_stat;
    size_t actual_size = RTSA_BUFFER_SIZE;

    // 获取实际传输长度（若 PL 产生 TLAST 短包传输）
    dma_stat = dmaengine_tx_status(rtsa->rx_chan, slot->cookie, &state);
    if (dma_stat != DMA_ERROR && state.residue <= RTSA_BUFFER_SIZE) {
        actual_size = RTSA_BUFFER_SIZE - state.residue;
    }

    spin_lock_irqsave(&rtsa->lock, flags);
    if (!rtsa->is_running) {
        slot->state = SLOT_STATE_FREE;
        spin_unlock_irqrestore(&rtsa->lock, flags);
        return;
    }

    slot->info.sequence = ++rtsa->frame_seq;
    slot->info.timestamp_ns = ktime_get_ns();
    slot->info.size = actual_size;
    slot->state = SLOT_STATE_COMPLETED;

    // 环形完成队列：写入完成槽号并进行溢出检测
    next_head = (rtsa->completed_head + 1) % RTSA_RING_BUFFER_COUNT;
    if (next_head == rtsa->completed_tail) {
        slot->info.status = 1; // 队列溢出（上位机未及时消费）
        dev_warn_ratelimited(&rtsa->pdev->dev,
                             "Ring buffer overflow, slot %u frame dropped\n", slot->index);
        // 关键修复：既然上位机完成队列已满，必须立刻将该 slot 重新归还给 DMA 硬件继续接收！
        // 否则该 slot 就会从硬件接收流中永久丢失，导致 DMA 引擎在 16 次溢出后彻底饿死停摆！
        slot->state = SLOT_STATE_FREE;
        spin_unlock_irqrestore(&rtsa->lock, flags);
        rtsa_submit_rx_slot(rtsa, slot->index);
        return;
    } else {
        slot->info.status = 0;
        rtsa->completed_queue[rtsa->completed_head] = slot->index;
        rtsa->completed_head = next_head;
    }

    spin_unlock_irqrestore(&rtsa->lock, flags);
    wake_up_interruptible(&rtsa->rx_wait);
}

// 提交一个 DMA 接收描述符到 DMAEngine
static int rtsa_submit_rx_slot(struct rtsa_dma_dev *rtsa, __u32 slot_idx) {
    struct dma_async_tx_descriptor *desc;
    struct dma_slot *slot;

    if (slot_idx >= RTSA_RING_BUFFER_COUNT)
        return -EINVAL;

    slot = &rtsa->slots[slot_idx];

    desc = dmaengine_prep_slave_single(
        rtsa->rx_chan,
        slot->paddr,
        RTSA_BUFFER_SIZE,
        DMA_DEV_TO_MEM,
        DMA_PREP_INTERRUPT | DMA_CTRL_ACK
    );

    if (!desc) {
        dev_err(&rtsa->pdev->dev, "Slot %u failed to prepare DMA descriptor\n", slot_idx);
        return -ENOMEM;
    }

    desc->callback = rtsa_dma_rx_callback;
    desc->callback_param = slot; // 关键修复：绑定当前 slot 结构体指针

    slot->state = SLOT_STATE_QUEUED;
    slot->cookie = dmaengine_submit(desc);
    if (dma_submit_error(slot->cookie)) {
        slot->state = SLOT_STATE_FREE;
        dev_err(&rtsa->pdev->dev, "Slot %u DMA submit error\n", slot_idx);
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
    if (!rtsa->is_running) {
        mask |= EPOLLHUP | EPOLLERR;
    } else if (rtsa->completed_head != rtsa->completed_tail) {
        mask |= EPOLLIN | EPOLLRDNORM;
    }
    spin_unlock_irqrestore(&rtsa->lock, flags);

    return mask;
}

static long rtsa_dma_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct rtsa_dma_dev *rtsa = filp->private_data;
    unsigned long flags;
    int ret = 0;
    int i;

    switch (cmd) {
    case RTSA_IOC_START: {

        spin_lock_irqsave(&rtsa->lock, flags);
        if (rtsa->is_running) {
            spin_unlock_irqrestore(&rtsa->lock, flags);
            return -EBUSY;
        }
        rtsa->is_running = true;
        rtsa->completed_head = 0;
        rtsa->completed_tail = 0;
        for (i = 0; i < RTSA_RING_BUFFER_COUNT; i++) {
            rtsa->slots[i].state = SLOT_STATE_FREE;
        }
        spin_unlock_irqrestore(&rtsa->lock, flags);

        // 提交所有 128 个环形缓冲区到 DMA 引擎
        for (i = 0; i < RTSA_RING_BUFFER_COUNT; i++) {
            ret = rtsa_submit_rx_slot(rtsa, i);
            if (ret) {
                dev_err(&rtsa->pdev->dev, "Failed to submit initial slot %d\n", i);
                spin_lock_irqsave(&rtsa->lock, flags);
                rtsa->is_running = false;
                spin_unlock_irqrestore(&rtsa->lock, flags);
                dmaengine_terminate_sync(rtsa->rx_chan);
                return ret;
            }
        }
        break;
    }

    case RTSA_IOC_STOP: {
        spin_lock_irqsave(&rtsa->lock, flags);
        rtsa->is_running = false;
        for (i = 0; i < RTSA_RING_BUFFER_COUNT; i++) {
            rtsa->slots[i].state = SLOT_STATE_FREE;
        }
        spin_unlock_irqrestore(&rtsa->lock, flags);

        dmaengine_terminate_sync(rtsa->rx_chan);
        wake_up_interruptible_all(&rtsa->rx_wait);
        break;
    }

    case RTSA_IOC_DEQUEUE: {
        struct rtsa_buffer_info info;
        __u32 slot_idx;

        memset(&info, 0, sizeof(info)); // 清零填充字节，杜绝内核栈信息泄露

        spin_lock_irqsave(&rtsa->lock, flags);
        if (rtsa->completed_head == rtsa->completed_tail) {
            spin_unlock_irqrestore(&rtsa->lock, flags);
            return -EAGAIN;
        }
        slot_idx = rtsa->completed_queue[rtsa->completed_tail];
        rtsa->completed_tail = (rtsa->completed_tail + 1) % RTSA_RING_BUFFER_COUNT;

        info = rtsa->slots[slot_idx].info;
        info.index = slot_idx;
        rtsa->slots[slot_idx].state = SLOT_STATE_FREE;
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

        spin_lock_irqsave(&rtsa->lock, flags);
        if (!rtsa->is_running) {
            spin_unlock_irqrestore(&rtsa->lock, flags);
            return -ENODEV;
        }
        if (rtsa->slots[slot_idx].state == SLOT_STATE_QUEUED) {
            spin_unlock_irqrestore(&rtsa->lock, flags);
            return -EBUSY; // 杜绝重复入队
        }
        // 锁内直接预置状态，彻底杜绝重入与并发竞态
        rtsa->slots[slot_idx].state = SLOT_STATE_QUEUED;
        spin_unlock_irqrestore(&rtsa->lock, flags);

        ret = rtsa_submit_rx_slot(rtsa, slot_idx);
        if (ret) {
            spin_lock_irqsave(&rtsa->lock, flags);
            rtsa->slots[slot_idx].state = SLOT_STATE_FREE;
            spin_unlock_irqrestore(&rtsa->lock, flags);
        }
        break;
    }

    default:
        ret = -ENOTTY;
    }

    return ret;
}

static int rtsa_dma_open(struct inode *inode, struct file *filp) {
    struct rtsa_dma_dev *rtsa = container_of(inode->i_cdev, struct rtsa_dma_dev, cdev);

    // 单进程排他性打开保护
    if (test_and_set_bit(0, &rtsa->is_opened))
        return -EBUSY;

    filp->private_data = rtsa;
    return 0;
}

// 释放函数：上位机进程退出或崩溃时彻底停止硬件
static int rtsa_dma_release(struct inode *inode, struct file *filp) {
    struct rtsa_dma_dev *rtsa = filp->private_data;
    unsigned long flags;
    int i;

    spin_lock_irqsave(&rtsa->lock, flags);
    rtsa->is_running = false;
    for (i = 0; i < RTSA_RING_BUFFER_COUNT; i++) {
        rtsa->slots[i].state = SLOT_STATE_FREE;
    }
    spin_unlock_irqrestore(&rtsa->lock, flags);

    if (rtsa->rx_chan)
        dmaengine_terminate_sync(rtsa->rx_chan);

    wake_up_interruptible_all(&rtsa->rx_wait);
    clear_bit(0, &rtsa->is_opened);
    return 0;
}

static const struct file_operations rtsa_dma_fops = {
    .owner          = THIS_MODULE,
    .open           = rtsa_dma_open,
    .release        = rtsa_dma_release,
    .mmap           = rtsa_dma_mmap,
    .poll           = rtsa_dma_poll,
    .unlocked_ioctl = rtsa_dma_ioctl,
    .compat_ioctl   = rtsa_dma_ioctl,
};

// ===== 安全的 probe（配置 DMA 掩码 + 一次申请 + 错误回滚） =====
static int rtsa_dma_probe(struct platform_device *pdev) {
    struct rtsa_dma_dev *rtsa;
    int ret, i;

    // 配置 64 位 DMA 寻址掩码（兼容 ZU47DR 高内存），失败则回退到 32 位
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
    if (ret) {
        ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
        if (ret) {
            dev_err(&pdev->dev, "Failed to set DMA mask\n");
            return ret;
        }
    }

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

    // 2. 一次性申请所有缓冲区（物理连续内存）
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

    // 3. 将大块内存切分成虚拟环，每个 slot 关联反向指针与偏移地址
    for (i = 0; i < RTSA_RING_BUFFER_COUNT; i++) {
        rtsa->slots[i].rtsa = rtsa;
        rtsa->slots[i].index = i;
        rtsa->slots[i].vaddr = rtsa->dma_base_vaddr + i * RTSA_BUFFER_SIZE;
        rtsa->slots[i].paddr = rtsa->dma_base_paddr + i * RTSA_BUFFER_SIZE;
        rtsa->slots[i].state = SLOT_STATE_FREE;
        memset(&rtsa->slots[i].info, 0, sizeof(rtsa->slots[i].info));
    }

    // 4. 注册字符设备
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    rtsa->class = class_create(DRIVER_NAME);
#else
    rtsa->class = class_create(THIS_MODULE, DRIVER_NAME);
#endif
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
    dev_info(&pdev->dev, "RTSA DMA driver probed successfully, total size: %zu bytes\n",
             rtsa->dma_total_size);
    return 0;

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

// ===== 安全的 remove（无条件停止硬件 + 资源回收） =====
static int rtsa_dma_remove(struct platform_device *pdev) {
    struct rtsa_dma_dev *rtsa = platform_get_drvdata(pdev);

    // 1. 无条件先停止硬件通道，防止后续释放引起总线异常
    if (rtsa->rx_chan) {
        dmaengine_terminate_sync(rtsa->rx_chan);
    }
    rtsa->is_running = false;
    wake_up_interruptible_all(&rtsa->rx_wait);

    // 2. 销毁字符设备节点
    device_destroy(rtsa->class, rtsa->dev_node);
    class_destroy(rtsa->class);
    cdev_del(&rtsa->cdev);
    unregister_chrdev_region(rtsa->dev_node, 1);

    // 3. 释放大块连续 DMA 物理内存
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