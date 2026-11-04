/**
 * @file  hdi.c
 * @brief host 一侧的 HDI 驱动库的实现
 *
 * @see spec-hdi
 */

#include "bm/hdi.h"

#include "bm/mmio.h"
#include "bm/test.h"

/** 时间戳频率：100 MHz */
#define TS_HZ           100000000ull

/** 读 HDI 寄存器 off */
uint32_t
hdi_read(const hdi_t *h, unsigned off)
{
    return bm_read32(h->regs + off);
}

/** 写 HDI 寄存器 off */
void
hdi_write(const hdi_t *h, unsigned off, uint32_t v)
{
    bm_write32(h->regs + off, v);
}

/**
 * 写 HDI_CC 并等待 BUSY = 0（spec-hdi §5.5），上限 HDI_CAP_LO.TO ×
 * 500 ms；返回之后的 HDI_CSTS，超时返回 -1
 */
int64_t
hdi_cc_write(hdi_t *h, uint32_t cc)
{
    uint64_t limit = (hdi_read(h, HDI_CAP_LO) >> 24) * (TS_HZ / 2);
    hdi_write(h, HDI_CC, cc);
    uint64_t t0 = gw100_timestamp(h->g);
    for (;;) {
        uint32_t st = hdi_read(h, HDI_CSTS);
        if (!(st & HDI_CSTS_BUSY))
            return st;
        if (gw100_timestamp(h->g) - t0 > limit)
            return -1;
    }
}

/**
 * 初始化 host 一侧的队列：SQ 在 sq（sq_n 条），CQ 在 cq（cq_n 条，
 * 清零），doorbell 页为第 page 页
 */
void
hdi_queue_init(const hdi_t *h, hdi_queue_t *q, uintptr_t sq, uint16_t sq_n,
               uintptr_t cq, uint16_t cq_n, unsigned page)
{
    q->sq = sq;
    q->cq = cq;
    q->sq_n = sq_n;
    q->cq_n = cq_n;
    q->sq_tail = 0;
    q->sq_head = 0;
    q->cq_head = 0;
    q->phase = 1;
    q->db = h->g->bar0 + GW100_DB(page);
    for (unsigned i = 0; i < 16u * cq_n; i += 8)
        *(volatile uint64_t *)(cq + i) = 0;
    bm_dsb();
}

/**
 * 按 spec-hdi §5.2 第 3 – 6 步使能控制器：检查版本，确认控制器处于
 * 复位完成状态（否则先写 EN = 0），建立管理队列（SQ、CQ 在 mem 起的
 * 两个 4 KiB 对齐的区域），写 EN = 1 并等待 RDY。成功返回 0
 */
int
hdi_enable(hdi_t *h, const gw100_t *g, uintptr_t mem, uint16_t sq_n,
           uint16_t cq_n)
{
    h->g = g;
    h->regs = g->bar0 + GW100_HDI;
    h->cid = 1;
    if (hdi_read(h, HDI_VS) != 0x00000100u)
        return -1;
    uint32_t st = hdi_read(h, HDI_CSTS);
    if ((hdi_read(h, HDI_CC) & HDI_CC_EN) || (st & 0x1F)) {
        int64_t r = hdi_cc_write(h, 0);
        if (r < 0 || (r & (HDI_CSTS_RDY | HDI_CSTS_CFS)))
            return -2;
    }
    uintptr_t sq = mem;
    uintptr_t cq = mem + ((64u * sq_n + 0xFFF) & ~0xFFFu);
    hdi_queue_init(h, &h->admin, sq, sq_n, cq, cq_n, 0);
    hdi_write(h, HDI_AQA, (uint32_t)(sq_n - 1) | (uint32_t)(cq_n - 1) << 16);
    hdi_write(h, HDI_ASQ_LO, (uint32_t)sq);
    hdi_write(h, HDI_ASQ_HI, (uint32_t)((uint64_t)sq >> 32));
    hdi_write(h, HDI_ACQ_LO, (uint32_t)cq);
    hdi_write(h, HDI_ACQ_HI, (uint32_t)((uint64_t)cq >> 32));
    int64_t r = hdi_cc_write(h, HDI_CC_EN);
    if (r < 0 || (r & HDI_CSTS_CFS) || !(r & HDI_CSTS_RDY))
        return -3;
    return 0;
}

/** 清零 sqe 并填入操作码 opc */
void
hdi_sqe(uint32_t sqe[16], unsigned opc)
{
    for (unsigned i = 0; i < 16; i++)
        sqe[i] = 0;
    sqe[0] = opc & 0xFF;
}

/** 把 DPTR 设为一个 SGL 条目：地址 addr、长度 len、类型 type */
void
hdi_dptr(uint32_t sqe[16], uint64_t addr, uint32_t len, unsigned type)
{
    sqe[6] = (uint32_t)addr;
    sqe[7] = (uint32_t)(addr >> 32);
    sqe[8] = len;
    sqe[9] = type & 0xFF;
}

/** 写一个 SGL 的 DATA 条目到 at */
void
hdi_sgl_data(uintptr_t at, uint64_t addr, uint32_t len)
{
    *(volatile uint64_t *)at = addr;
    *(volatile uint32_t *)(at + 8) = len;
    *(volatile uint32_t *)(at + 12) = HDI_SGL_DATA;
}

/**
 * 填入 CID，把 sqe 写到 q 的 SQ 尾，推进尾指针并写 SQ_TAIL；返回 CID。
 * 调用者保证 SQ 不满
 */
uint16_t
hdi_submit(hdi_t *h, hdi_queue_t *q, uint32_t sqe[16])
{
    uint16_t cid = h->cid++;
    if (h->cid == 0)
        h->cid = 1;
    sqe[0] = (sqe[0] & 0xFFFF) | (uint32_t)cid << 16;
    volatile uint32_t *s = (volatile uint32_t *)(q->sq + 64u * q->sq_tail);
    for (unsigned i = 0; i < 16; i++)
        s[i] = sqe[i];
    if (++q->sq_tail == q->sq_n)
        q->sq_tail = 0;
    /* SQE 对设备可见之后再写 doorbell（spec-hdi §6.3） */
    bm_dsb();
    bm_write32(q->db + HDI_DB_SQ_TAIL, q->sq_tail);
    return cid;
}

/**
 * 读取 q 的下一条 CQE（不写 CQ_HEAD）：没有新条目返回 0，否则填入 e、
 * 推进头指针并返回 1
 */
int
hdi_reap(hdi_queue_t *q, hdi_cqe_t *e)
{
    volatile uint32_t *c = (volatile uint32_t *)(q->cq + 16u * q->cq_head);
    uint32_t dw3 = c[3];
    if (((dw3 >> 16) & 1) != q->phase)
        return 0;
    /* 先读 DW3，确认是新条目之后再读其余各字（spec-hdi §6.5） */
    bm_dmb();
    e->res_lo = c[0];
    e->res_hi = c[1];
    e->sqhd = (uint16_t)c[2];
    e->cid = (uint16_t)dw3;
    e->status = (uint16_t)(((dw3 >> 17) & 0xFF) | ((dw3 >> 25) & 7) << 8);
    e->dnr = (uint8_t)(dw3 >> 31);
    q->sq_head = e->sqhd;
    if (++q->cq_head == q->cq_n) {
        q->cq_head = 0;
        q->phase ^= 1;
    }
    return 1;
}

/** 写 q 的 CQ_HEAD */
void
hdi_cq_doorbell(const hdi_queue_t *q)
{
    bm_write32(q->db + HDI_DB_CQ_HEAD, q->cq_head);
}

/**
 * 等待 q 的下一条 CQE 并写 CQ_HEAD，上限 timeout 个时间戳周期；成功
 * 返回 0，超时返回 -1
 */
int
hdi_wait(hdi_t *h, hdi_queue_t *q, hdi_cqe_t *e, uint64_t timeout)
{
    uint64_t t0 = gw100_timestamp(h->g);
    while (!hdi_reap(q, e)) {
        if (gw100_timestamp(h->g) - t0 > timeout)
            return -1;
    }
    hdi_cq_doorbell(q);
    return 0;
}

/**
 * 提交一条命令并等待它的完成记录（队列中没有其他未完成的命令），
 * 返回状态；超时或 CID 不符返回 -1
 */
int
hdi_cmd(hdi_t *h, hdi_queue_t *q, uint32_t sqe[16], hdi_cqe_t *e)
{
    uint16_t cid = hdi_submit(h, q, sqe);
    if (hdi_wait(h, q, e, TS_HZ / 10) != 0)
        return -1;
    if (e->cid != cid)
        return -1;
    return e->status;
}
