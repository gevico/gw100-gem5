/**
 * @file  hdi.h
 * @brief host 一侧的 HDI 驱动库：控制器的使能与复位、队列、命令与完成
 *
 * 只做裸机测试需要的部分，按 spec-hdi 的驱动规则实现：写 HDI_CC 之后
 * 等 BUSY = 0（§5.5）；SQE 写好之后才写 SQ_TAIL（§6.3）；按相位位
 * 读取 CQE，先读 DW3、再读其余各字（§6.5）。MMU 关闭时 host 的物理
 * 地址就是总线地址，队列和缓冲直接用物理地址。
 *
 * @see spec-hdi
 */

#ifndef BM_HDI_H
#define BM_HDI_H

#include <stdint.h>

#include "bm/gw100.h"

/* HDI 控制器寄存器，相对于 GW100_HDI（spec-hdi §4） */
#define HDI_CAP_LO              0x000
#define HDI_CAP_HI              0x004
#define HDI_VS                  0x008
#define HDI_CC                  0x010
#define HDI_CSTS                0x014
#define HDI_AQA                 0x018
#define HDI_ASQ_LO              0x020
#define HDI_ASQ_HI              0x024
#define HDI_ACQ_LO              0x028
#define HDI_ACQ_HI              0x02C
#define HDI_FATAL               0x030
#define HDI_CTX_ERR             0x034

/* HDI_CC、HDI_CSTS 的位 */
#define HDI_CC_EN               (1u << 0)
/** HDI_CC.SHN 的值 v */
#define HDI_CC_SHN(v)           ((uint32_t)(v) << 14)
#define HDI_CSTS_RDY            (1u << 0)
#define HDI_CSTS_CFS            (1u << 1)
/** 从 HDI_CSTS 的值 v 取出 SHST */
#define HDI_CSTS_SHST(v)        (((v) >> 2) & 3)
#define HDI_CSTS_BUSY           (1u << 4)

/* doorbell 页内的偏移（spec-hdi §4.2） */
#define HDI_DB_SQ_TAIL          0x000
#define HDI_DB_CQ_HEAD          0x004

/* 操作码（spec-hdi §9、§10） */
#define HDI_OP_NOP              0x00
#define HDI_OP_IDENTIFY         0x01
#define HDI_OP_CREATE_CTX       0x02
#define HDI_OP_DESTROY_CTX      0x03
#define HDI_OP_SET_REGION       0x04
#define HDI_OP_SET_EVENTS       0x05
#define HDI_OP_GET_FEATURE      0x06
#define HDI_OP_SET_FEATURE      0x07
#define HDI_OP_GET_LOG          0x08
#define HDI_OP_ABORT            0x09
#define HDI_OP_CTX_NOP          0x40
#define HDI_OP_COPY_H2D         0x41
#define HDI_OP_COPY_D2H         0x42
#define HDI_OP_FILL             0x43
#define HDI_OP_SIGNAL           0x50
#define HDI_OP_WAIT             0x51
#define HDI_OP_TIMESTAMP        0x52

/* 状态：SCT << 8 | SC（spec-hdi §7.3） */
#define HDI_SC_SUCCESS          0x00
#define HDI_SC_INVALID_OPCODE   0x01
#define HDI_SC_INVALID_FIELD    0x02
#define HDI_SC_INVALID_CONTEXT  0x03
#define HDI_SC_ACCESS_VIOLATION 0x04
#define HDI_SC_SGL_INVALID      0x05
#define HDI_SC_DATA_XFER        0x06
#define HDI_SC_ABORTED_REQ      0x08
#define HDI_SC_ABORTED_CTX      0x09
#define HDI_SC_TIMEOUT          0x0C
#define HDI_SC_QUEUE_INVALID    0x100

/* SGL 条目的 TYPE（spec-host-dma §4.1） */
#define HDI_SGL_DATA            0
#define HDI_SGL_SEGMENT         1

/* 区间权限（spec-hdi §9.5） */
#define HDI_PERM_R              (1u << 0)
#define HDI_PERM_W              (1u << 1)
#define HDI_PERM_X              (1u << 2)

/** 一对队列（SQ、CQ）在 host 一侧的状态 */
typedef struct {
    /** SQ 的地址 */
    uintptr_t sq;
    /** CQ 的地址 */
    uintptr_t cq;
    /** SQ 的条目数 */
    uint16_t sq_n;
    /** CQ 的条目数 */
    uint16_t cq_n;
    /** SQ 尾：下一条 SQE 的位置 */
    uint16_t sq_tail;
    /** 设备报告的 SQ 头（最近一条 CQE 的 SQHD） */
    uint16_t sq_head;
    /** CQ 头：下一条要读的 CQE */
    uint16_t cq_head;
    /** 期望的相位位 */
    uint8_t phase;
    /** doorbell 页的 CPU 地址 */
    uintptr_t db;
} hdi_queue_t;

/** 一条完成记录 */
typedef struct {
    /** RESULT_LO */
    uint32_t res_lo;
    /** RESULT_HI */
    uint32_t res_hi;
    /** SQHD */
    uint16_t sqhd;
    /** CID */
    uint16_t cid;
    /** SCT << 8 | SC */
    uint16_t status;
    /** DNR */
    uint8_t dnr;
} hdi_cqe_t;

/** HDI 设备 */
typedef struct {
    /** 设备 */
    const gw100_t *g;
    /** HDI 控制器寄存器的 CPU 地址 */
    uintptr_t regs;
    /** 管理队列 */
    hdi_queue_t admin;
    /** 下一个 CID */
    uint16_t cid;
} hdi_t;

/** 读 HDI 寄存器 off */
uint32_t hdi_read(const hdi_t *h, unsigned off);

/** 写 HDI 寄存器 off */
void hdi_write(const hdi_t *h, unsigned off, uint32_t v);

/**
 * 写 HDI_CC 并等待 BUSY = 0（spec-hdi §5.5），上限 HDI_CAP_LO.TO ×
 * 500 ms；返回之后的 HDI_CSTS，超时返回 -1
 */
int64_t hdi_cc_write(hdi_t *h, uint32_t cc);

/**
 * 初始化 host 一侧的队列：SQ 在 sq（sq_n 条），CQ 在 cq（cq_n 条，
 * 清零），doorbell 页为第 page 页
 */
void hdi_queue_init(const hdi_t *h, hdi_queue_t *q, uintptr_t sq,
                    uint16_t sq_n, uintptr_t cq, uint16_t cq_n,
                    unsigned page);

/**
 * 按 spec-hdi §5.2 第 3 – 6 步使能控制器：检查版本，确认控制器处于
 * 复位完成状态（否则先写 EN = 0），建立管理队列（SQ、CQ 在 mem 起的
 * 两个 4 KiB 对齐的区域），写 EN = 1 并等待 RDY。成功返回 0
 */
int hdi_enable(hdi_t *h, const gw100_t *g, uintptr_t mem, uint16_t sq_n,
               uint16_t cq_n);

/** 清零 sqe 并填入操作码 opc */
void hdi_sqe(uint32_t sqe[16], unsigned opc);

/** 把 DPTR 设为一个 SGL 条目：地址 addr、长度 len、类型 type */
void hdi_dptr(uint32_t sqe[16], uint64_t addr, uint32_t len, unsigned type);

/** 写一个 SGL 的 DATA 条目到 at */
void hdi_sgl_data(uintptr_t at, uint64_t addr, uint32_t len);

/**
 * 填入 CID，把 sqe 写到 q 的 SQ 尾，推进尾指针并写 SQ_TAIL；返回 CID。
 * 调用者保证 SQ 不满
 */
uint16_t hdi_submit(hdi_t *h, hdi_queue_t *q, uint32_t sqe[16]);

/**
 * 读取 q 的下一条 CQE（不写 CQ_HEAD）：没有新条目返回 0，否则填入 e、
 * 推进头指针并返回 1
 */
int hdi_reap(hdi_queue_t *q, hdi_cqe_t *e);

/** 写 q 的 CQ_HEAD */
void hdi_cq_doorbell(const hdi_queue_t *q);

/**
 * 等待 q 的下一条 CQE 并写 CQ_HEAD，上限 timeout 个时间戳周期；成功
 * 返回 0，超时返回 -1
 */
int hdi_wait(hdi_t *h, hdi_queue_t *q, hdi_cqe_t *e, uint64_t timeout);

/**
 * 提交一条命令并等待它的完成记录（队列中没有其他未完成的命令），
 * 返回状态；超时或 CID 不符返回 -1
 */
int hdi_cmd(hdi_t *h, hdi_queue_t *q, uint32_t sqe[16], hdi_cqe_t *e);

#endif /* BM_HDI_H */
