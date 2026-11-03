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
/** EVT_PENDING（RW1C） */
#define GW_EVT_PENDING      (GW_MBOX_BASE + 0x200)
/** EVT_MASK */
#define GW_EVT_MASK         (GW_MBOX_BASE + 0x204)
/** 事件位：FW_START */
#define GW_EVT_FW_START     (1u << 0)
/** 事件位：FLR */
#define GW_EVT_FLR          (1u << 1)

/** CRC 寄存器的起始 */
#define GW_CRC_BASE         0x20020000UL
/**
 * CRC_RESET_CAUSE：[0] POR，[1] PERST，[2] HOT_RESET，[3] LINK_DOWN，
 * [4] DEV_RESET
 */
#define GW_CRC_RESET_CAUSE  (GW_CRC_BASE + 0x000)

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
/** BOOT_LOAD_STATUS：[0] WIN_WRITE */
#define GW_BOOT_LOAD_STATUS (GW_HIF_BASE + 0x10014)
/** FLR_CTRL：[0] TAKE，[1] DONE（W1S） */
#define GW_FLR_CTRL         (GW_HIF_BASE + 0x12010)
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

#endif /* GW100_MGMT_H */
