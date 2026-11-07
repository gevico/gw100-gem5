/**
 * @file  gw100_mgmt.h
 * @brief 管理 CPU 看到的寄存器地址与字段（启动 ROM 与固件共用）
 *
 * 地址取自 spec-mgmt-tile §3.3、§3.5 和 spec-pcie §6.5、§13.2。
 *
 * @see spec-mgmt-tile, spec-pcie, spec-mgmt-link
 */

#ifndef GW100_MGMT_H
#define GW100_MGMT_H

#include <stdint.h>

/** 启动 ROM 的本地地址 */
#define GW_ROM_BASE         0x00000000UL
/** SRAM 的本地地址 */
#define GW_SRAM_BASE        0x10000000UL
/** SRAM 的大小（mgmt.sram_size） */
#define GW_SRAM_SIZE        (2UL << 20)
/** 装载区的大小（mgmt.fw_load_max） */
#define GW_FW_LOAD_MAX      (1UL << 20)
/** ROM 保留区的大小（mgmt.rom_reserved） */
#define GW_ROM_RESERVED     (4UL << 10)
/** ROM 保留区的起始：SRAM 末尾 */
#define GW_ROM_RSV_BASE     (GW_SRAM_BASE + GW_SRAM_SIZE - GW_ROM_RESERVED)
/** 运行区的起始：装载区之后 */
#define GW_RUN_BASE         (GW_SRAM_BASE + GW_FW_LOAD_MAX)

/** CLINT：mtime（64 位，只读） */
#define GW_CLINT_MTIME      0x2000BFF8UL
/** 看门狗寄存器的起始 */
#define GW_WDT_BASE         0x2000C000UL
/** WDT_CTRL：[0] EN，[1] LOCK */
#define GW_WDT_CTRL         (GW_WDT_BASE + 0x00)
/** WDT_LOAD（64 位）：超时周期，以 mtime 计 */
#define GW_WDT_LOAD         (GW_WDT_BASE + 0x08)
/** WDT_KICK（只写） */
#define GW_WDT_KICK         (GW_WDT_BASE + 0x10)
/** WDT_STATUS：[0] STAGE1，[1] STAGE2 */
#define GW_WDT_STATUS       (GW_WDT_BASE + 0x14)
/** WDT_CTRL.EN */
#define GW_WDT_EN           (1u << 0)
/** WDT_CTRL.LOCK */
#define GW_WDT_LOCK         (1u << 1)
/** WDT_STATUS.STAGE1 */
#define GW_WDT_STAGE1       (1u << 0)
/** 合法喂狗写入 WDT_KICK 的值 */
#define GW_WDT_KICK_MAGIC   0x5A5AA5A5u

/** PLIC 的中断挂起位（源 0 – 31） */
#define GW_PLIC_PENDING     0x2C001000UL
/** PLIC 源：看门狗第一级超时（spec-mgmt-tile §7.2） */
#define GW_IRQ_WDT          7

/** ML 寄存器的起始 ML_BASE */
#define GW_ML_BASE          0x2000D000UL
/** ML_STATUS：[0] UP，[1] FAILED */
#define GW_ML_STATUS        (GW_ML_BASE + 0x00)
/** ML_ERR */
#define GW_ML_ERR           (GW_ML_BASE + 0x04)
/** SRAM_STATUS：[0] INIT_DONE */
#define GW_SRAM_STATUS      0x2000E000UL

/** doorbell 信箱与事件寄存器的起始 */
#define GW_MBOX_BASE        0x20010000UL
/** DB_SLOT[i]：第 i 个 doorbell 最近一次写入的值 */
#define GW_DB_SLOT(i)       (GW_MBOX_BASE + 4 * (i))
/** DB_PENDING[0]（RW1C）：槽 0 – 31 */
#define GW_DB_PENDING0      (GW_MBOX_BASE + 0x100)
/** DB_PENDING[1]（RW1C）：槽 32 – 33 */
#define GW_DB_PENDING1      (GW_MBOX_BASE + 0x104)
/** DB_MASK[0] */
#define GW_DB_MASK0         (GW_MBOX_BASE + 0x108)
/** DB_MASK[1] */
#define GW_DB_MASK1         (GW_MBOX_BASE + 0x10C)
/** EVT_PENDING（RW1C） */
#define GW_EVT_PENDING      (GW_MBOX_BASE + 0x200)
/** EVT_MASK */
#define GW_EVT_MASK         (GW_MBOX_BASE + 0x204)
/** 事件位：FW_START */
#define GW_EVT_FW_START     (1u << 0)
/** 事件位：FLR */
#define GW_EVT_FLR          (1u << 1)
/** 事件位：HDI_WR（host 写了 HDI 控制器的可写寄存器） */
#define GW_EVT_HDI_WR       (1u << 2)
/** 事件位：电源状态改变 */
#define GW_EVT_PM (1u << 4)

/** CRC 寄存器的起始 */
#define GW_CRC_BASE         0x20020000UL
/**
 * CRC_RESET_CAUSE：[0] POR，[1] PERST，[2] HOT_RESET，[3] LINK_DOWN，
 * [4] DEV_RESET
 */
#define GW_CRC_RESET_CAUSE  (GW_CRC_BASE + 0x000)
/** CRC_PLL_CORE_CFG：[0] EN */
#define GW_CRC_PLL_CORE_CFG (GW_CRC_BASE + 0x030)
/** CRC_MESH_RST：[0] NOC，[4:1] MEM，写零释放 */
#define GW_CRC_MESH_RST (GW_CRC_BASE + 0x010)
/** CRC_PLL_DDR_CFG：[0] EN，[3:1] 通用工作点编号 */
#define GW_CRC_PLL_DDR_CFG (GW_CRC_BASE + 0x034)
/** CRC_PLL_STATUS：[0] CORE_LOCK，[1] DDR_LOCK，[2] TS_LOCK */
#define GW_CRC_PLL_STATUS   (GW_CRC_BASE + 0x038)
/** CRC_CLK_SEL：[0] CORE，[1] MGMT；1 为 PLL_CORE */
#define GW_CRC_CLK_SEL      (GW_CRC_BASE + 0x03C)
/** CRC_PLL_STATUS.CORE_LOCK */
#define GW_PLL_CORE_LOCK    (1u << 0)
/** CRC_PLL_STATUS.DDR_LOCK */
#define GW_PLL_DDR_LOCK (1u << 1)
/** CRC_PLL_STATUS.TS_LOCK */
#define GW_PLL_TS_LOCK      (1u << 2)
/** clock.pll_lock_timeout = 1 ms，以 mtime（100 MHz）计 */
#define GW_PLL_LOCK_TIMEOUT 100000UL
/**
 * clock.pll_lock_timeout = 1 ms，以 XTAL（25 MHz）下的 mcycle 计；用于
 * 等待 PLL_TS，此时 mtime 还不一定在走（spec-mgmt-tile §8.5）
 */
#define GW_TS_LOCK_CYCLES   25000UL

/** DPA 空间的起始：本地地址 = GW_NI_BASE + DPA，经 NI 访问 */
#define GW_NI_BASE          0x10000000000UL

/** HIF 寄存器区的起始（经 ML 访问，只支持 32 位） */
#define GW_HIF_BASE         0x30000000UL
/** FW_BOOT_STATUS：[3:0] STAGE，[8] FW_READY，[23:16] ERROR */
#define GW_FW_BOOT_STATUS   (GW_HIF_BASE + 0x10000)
/** FW_SIZE_FW：读 host 写入的 FW_SIZE；只能写 0 */
#define GW_FW_SIZE_FW       (GW_HIF_BASE + 0x10004)
/** FW_VERSION_FW */
#define GW_FW_VERSION_FW    (GW_HIF_BASE + 0x10008)
/** DEV_CTRL_FW：[0] DEBUG_WIN，[1] DEV_FATAL（只能置位） */
#define GW_DEV_CTRL_FW      (GW_HIF_BASE + 0x1000C)
/** PCI_STATE：[3:2] 电源状态，0 为 D0，3 为 D3hot */
#define GW_PCI_STATE (GW_HIF_BASE + 0x10010)
/** BOOT_LOAD_STATUS：[0] WIN_WRITE */
#define GW_BOOT_LOAD_STATUS (GW_HIF_BASE + 0x10014)
/** FLR_CTRL：[0] TAKE，[1] DONE（W1S） */
#define GW_FLR_CTRL         (GW_HIF_BASE + 0x12010)
/** HDI 控制器寄存器的固件视角：偏移 off（spec-pcie §6.5） */
#define GW_HDI_FW(off)      (GW_HIF_BASE + 0x11000 + (off))
/** ERR_REQ（W1S）：只接受 [4] DB_INVALID */
#define GW_ERR_REQ          (GW_HIF_BASE + 0x12008)
/** ERR_REQ 的位：DB_INVALID */
#define GW_ERR_DB_INVALID   (1u << 4)
/** INT_REQ（W1S）：请求 host 中断（spec-pcie §6.5） */
#define GW_INT_REQ          (GW_HIF_BASE + 0x12000)
/** INT_LEVEL：固件对中断源条件的贡献 */
#define GW_INT_LEVEL        (GW_HIF_BASE + 0x12004)
/** CTX_VECTOR[n]：上下文 n 的 MSI-X 向量 */
#define GW_CTX_VECTOR(n)    (GW_HIF_BASE + 0x12100 + 4 * (n))
/** INT_STATUS 的位：ADMIN */
#define GW_INT_ADMIN        (1u << 0)
/** INT_STATUS 的位：FW_EVENT */
#define GW_INT_FW_EVENT     (1u << 2)
/** INT_STATUS 的位：CTX[n] */
#define GW_INT_CTX(n)       (1u << (16 + (n)))
/** FLR_STATUS：[0] IN_PROGRESS，[1] DELIVERED，[2] FAIL */
#define GW_FLR_STATUS       (GW_HIF_BASE + 0x12014)

/** 启动阶段（spec-pcie §6.4） */
enum gw_stage {
    /** 复位中 */
    GW_STAGE_RESET = 0,
    /** ROM 初始化 */
    GW_STAGE_ROM_INIT = 1,
    /** 等待加载 */
    GW_STAGE_WAIT_LOAD = 2,
    /** 校验 */
    GW_STAGE_VERIFY = 3,
    /** 固件运行 */
    GW_STAGE_FW_RUN = 4,
    /** 启动失败 */
    GW_STAGE_FAILED = 0xF,
};

/** 启动错误码（spec-pcie §6.4） */
enum gw_boot_err {
    /** 无错误 */
    GW_ERR_NONE = 0,
    /** FW_SIZE 为 0、超过上限或不是 8 的倍数 */
    GW_ERR_SIZE = 1,
    /** 本次加载期间启动窗口收到过非法写入 */
    GW_ERR_WIN_WRITE = 2,
    /** 镜像校验失败 */
    GW_ERR_VERIFY = 3,
    /** 固件初始化失败 */
    GW_ERR_INIT = 4,
};

/** 由 STAGE、FW_READY、ERROR 拼出 FW_BOOT_STATUS 的值 */
#define GW_BOOT_STATUS(stage, ready, err) \
    ((uint32_t)(stage) | ((uint32_t)(ready) << 8) | ((uint32_t)(err) << 16))

/** 读本地地址 a 处的 32 位寄存器 */
static inline uint32_t
gw_read32(uintptr_t a)
{
    return *(volatile uint32_t *)a;
}

/** 写本地地址 a 处的 32 位寄存器 */
static inline void
gw_write32(uintptr_t a, uint32_t v)
{
    *(volatile uint32_t *)a = v;
}

/** 读本地地址 a 处的 64 位寄存器 */
static inline uint64_t
gw_read64(uintptr_t a)
{
    return *(volatile uint64_t *)a;
}

/** 写本地地址 a 处的 64 位寄存器 */
static inline void
gw_write64(uintptr_t a, uint64_t v)
{
    *(volatile uint64_t *)a = v;
}

/** 读 mcycle：clk_mgmt 的周期数 */
static inline uint64_t
gw_mcycle(void)
{
    uint64_t v;
    __asm__ volatile("csrr %0, mcycle" : "=r"(v));
    return v;
}

#endif /* GW100_MGMT_H */
