/**
 * @file  gw100_hdma.h
 * @brief 固件使用的 HDMA 寄存器、描述符格式和最小驱动
 *
 * HDMA 的寄存器在 HIF 区的起始（HDMA_BASE，spec-host-dma §7.1），经
 * ML 访问，只支持 32 位。描述符环放在 SRAM 运行区，由固件分配；
 * 完成按 spec-host-dma §5.3 的协议从 DW7 得知。
 *
 * @see spec-host-dma
 */

#ifndef GW100_HDMA_H
#define GW100_HDMA_H

#include <stdint.h>

#include "gw100_mgmt.h"

/** HDMA 寄存器的起始 HDMA_BASE */
#define GW_HDMA_BASE        GW_HIF_BASE
/** HDMA_CTRL：[0] EN，[1] ABORT_ALL */
#define GW_HDMA_CTRL        (GW_HDMA_BASE + 0x00C)
/** HDMA_CTRL.ABORT_ALL（W1S） */
#define GW_HDMA_ABORT_ALL   (1u << 1)
/** 通道 c 的寄存器 off */
#define GW_HDMA_CH(c, off)  (GW_HDMA_BASE + 0x100 + 0x40 * (c) + (off))
/** CH_CTRL：[0] EN，[1] PAUSE，[2] ABORT，[3] RESUME，[11:8] WEIGHT */
#define GW_CH_CTRL          0x00
/** CH_STATUS：[1:0] STATE，[8] RING_EMPTY */
#define GW_CH_STATUS        0x04
/** CH_RING_BASE_LO */
#define GW_CH_RING_LO       0x08
/** CH_RING_BASE_HI */
#define GW_CH_RING_HI       0x0C
/** CH_RING_SIZE：[3:0] LOG2_ENTRIES */
#define GW_CH_RING_SIZE     0x10
/** CH_TAIL */
#define GW_CH_TAIL          0x14
/** CH_HEAD */
#define GW_CH_HEAD          0x18
/** CH_ERR：[7:0] ERR_CODE，[9:8] PHASE，[31:16] 下标 */
#define GW_CH_ERR           0x1C
/** CH_CTRL.RESUME */
#define GW_CH_RESUME        (1u << 3)
/** CH_STATUS.STATE：错误停止 */
#define GW_CH_ERRSTOP       3u

/** 描述符 TYPE：H2D */
#define GW_DESC_H2D         1u
/** 描述符 TYPE：D2H */
#define GW_DESC_D2H         2u
/** DW0.HOST_SGL */
#define GW_DESC_SGL         (1u << 8)
/** DW0.NOTIFY */
#define GW_DESC_NOTIFY      (1u << 11)
/** DW0.FENCE */
#define GW_DESC_FENCE       (1u << 12)

/** 事件位：HDMA_NOTIFY */
#define GW_EVT_HDMA_NOTIFY  (1u << 5)
/** 事件位：HDMA_ERR */
#define GW_EVT_HDMA_ERR     (1u << 6)

/** 访问控制：ACL_LOCK（spec-mgmt-tile §4.4） */
#define GW_ACL_LOCK         0x2000F000UL
/** 访问控制：DESC_BASE（64 位） */
#define GW_ACL_DESC_BASE    0x2000F008UL
/** 访问控制：DESC_LIMIT（64 位） */
#define GW_ACL_DESC_LIMIT   0x2000F010UL

/** 一个描述符（spec-host-dma §3.2） */
struct gw_hdma_desc {
    /** DW0：TYPE、标志、TAG */
    uint32_t dw0;
    /** DW1：LENGTH */
    uint32_t len;
    /** DW2、DW3：HOST_ADDR */
    uint64_t host;
    /** DW4、DW5：DEV_ADDR */
    uint64_t dev;
    /** DW6：SGL_SEG_BYTES、NOTIFY_ID */
    uint32_t dw6;
    /** DW7：DONE、ERR_CODE，由 HDMA 回写 */
    volatile uint32_t dw7;
};

/** 一个通道的环和固件一侧的状态 */
struct gw_hdma_chan {
    /** 通道号 */
    unsigned ch;
    /** 环 */
    struct gw_hdma_desc *ring;
    /** 条目数 */
    unsigned entries;
    /** 固件最后写入的 TAIL */
    unsigned tail;
    /** 消费游标 C */
    unsigned consumed;
};

/** 初始化通道：环在 SRAM 中，entries 为 2 的幂，使能 */
void gw_hdma_chan_init(struct gw_hdma_chan *c, unsigned ch,
                       struct gw_hdma_desc *ring, unsigned entries);

/** 把描述符 d 写入环（DW7 清零），返回它的下标；不提交 */
unsigned gw_hdma_put(struct gw_hdma_chan *c, const struct gw_hdma_desc *d);

/** 提交已写入的描述符：写 CH_TAIL */
void gw_hdma_commit(struct gw_hdma_chan *c);

/**
 * 等待下标 idx 的描述符完成：按 §5.3 先清 HDMA_NOTIFY 再读 CH_HEAD，
 * 超时 timeout 个 mtime 周期。返回 DW7 的 ERR_CODE，超时返回 -1
 */
int gw_hdma_wait(struct gw_hdma_chan *c, unsigned idx, uint64_t timeout);

#endif /* GW100_HDMA_H */
