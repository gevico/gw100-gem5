/**
 * @file  gw100.h
 * @brief GW100 的寄存器定义与打开设备的助手
 *
 * 偏移和位定义照抄 spec-pcie；各 test 通过 gw100_open() 找到设备、
 * 分配 BAR 并使能内存解码，不各自重复枚举代码。
 *
 * @see spec-pcie
 */

#ifndef BM_GW100_H
#define BM_GW100_H

#include <stdint.h>

#include "bm/pci.h"

/* 配置空间（spec-pcie §4） */
#define GW100_VENDOR_ID         0x1234      /* 仿真用占位值 */
#define GW100_DEVICE_ID         0x1000
#define GW100_CLASS_BASE        0x12        /* Processing Accelerator */
#define GW100_BAR0_SIZE         0x100000ull /* 1 MiB */
#define GW100_BAR2_SIZE         0x4000000ull /* 64 MiB */
#define GW100_BAR2_WIN_COUNT    16
#define GW100_BAR2_WIN_SIZE     0x400000ull /* 4 MiB */

/* BAR0 全局寄存器（spec-pcie §6.1） */
#define GW100_DEV_VERSION       0x000
#define GW100_DEV_CAPS          0x004
#define GW100_DEV_STATUS        0x008
#define GW100_SCRATCH           0x00C
#define GW100_TIMESTAMP_LO      0x010
#define GW100_TIMESTAMP_HI      0x014
#define GW100_TIMESTAMP_FREQ    0x018
#define GW100_INT_STATUS        0x020
#define GW100_INT_MASK          0x024
#define GW100_INT_SET           0x028
#define GW100_ERR_STATUS        0x030
#define GW100_ERR_ADDR          0x034
#define GW100_ERR_INFO          0x038

/* INT_STATUS 的位（spec-pcie §8.4） */
#define GW100_INT_ADMIN         (1u << 0)
#define GW100_INT_ERROR         (1u << 1)
#define GW100_INT_FW_EVENT      (1u << 2)

/* ERR_STATUS 的位（spec-pcie §6.3） */
#define GW100_ERR_RSVD_ACCESS   (1u << 0)
#define GW100_ERR_BAD_WIDTH     (1u << 1)
#define GW100_ERR_WIN_DISABLED  (1u << 2)
#define GW100_ERR_WIN_TARGET    (1u << 3)
#define GW100_ERR_BOOT_WIN      (1u << 7)

/* DEV_STATUS 的位（spec-pcie §6.2） */
#define GW100_DEV_STATUS_DEBUG_WIN (1u << 17)

/* ERR_INFO 的字段 */
#define GW100_ERR_INFO_WRITE    (1u << 0)
/** 从 ERR_INFO 的值 v 取出访问字节数 */
#define GW100_ERR_INFO_SIZE(v)  (((v) >> 2) & 0xF)
/** 从 ERR_INFO 的值 v 取出 BAR 号 */
#define GW100_ERR_INFO_BAR(v)   (((v) >> 6) & 0x3)

/* BAR2 窗口配置（spec-pcie §7.2），窗口 i 位于 0x1000 + 0x10 × i */
/** 窗口 i 的配置寄存器在 BAR0 中的起始偏移 */
#define GW100_WIN_BASE(i)       (0x1000 + 0x10 * (i))
#define GW100_WIN_CTRL          0x0
#define GW100_WIN_BASE_LO       0x8
#define GW100_WIN_BASE_HI       0xC
#define GW100_WIN_EN            (1u << 0)
#define GW100_WIN_RD_ONLY       (1u << 1)

/* 启动寄存器（spec-pcie §6.4） */
#define GW100_BOOT_STATUS       0x4000
#define GW100_BOOT_CTRL         0x4004
#define GW100_FW_LOAD_MAX       0x4010
#define GW100_FW_SIZE           0x4014
#define GW100_FW_VERSION        0x4018
#define GW100_BOOT_FW_READY     (1u << 8)

/* 保留的块：本阶段尚未实现（MSI-X、HDI、DMA 镜像） */
#define GW100_RSVD_BLOCK_HDI    0x5000

/** 打开后的 GW100 */
typedef struct {
    /** 设备在 PCIe 上的位置 */
    bm_pci_bdf_t bdf;
    /** BAR0 的 CPU 地址 */
    uintptr_t bar0;
    /** BAR2 的 CPU 地址 */
    uintptr_t bar2;
} gw100_t;

/**
 * 查找 GW100，分配 BAR0、BAR2，使能内存解码。
 * 找不到设备或分配失败时直接以 FAIL 结束测试。
 */
gw100_t gw100_open(void);

/**
 * 把 BAR2 的窗口 i 指向设备物理地址 base，flags 为 GW100_WIN_* 的组合；
 * 配置后读回一次，确认生效
 */
void gw100_win_set(const gw100_t *g, unsigned i, uint64_t base,
                   uint32_t flags);

#endif /* BM_GW100_H */
