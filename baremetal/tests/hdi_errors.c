/**
 * @file  hdi_errors.c
 * @brief HDI：事件、中止、中断合并、doorbell 错误、致命错误与关闭
 *
 * 在 hdi_copy 覆盖的基本流程之外检查：
 * - FILL、TIMESTAMP；SIGNAL_EVENT 与另一个上下文的 WAIT_EVENT（满足、
 *   超时、无权限），ABORT 中止等待中的 WAIT_EVENT（§9.9、§10）；
 * - 中断合并：THR 未满时不通知、满时通知，TIME 到期通知，错误完成不受
 *   合并限制（§12.2）；
 * - doorbell 写入不存在的上下文、上下文的 doorbell 越界：DB_INVALID、
 *   上下文进入错误状态、HDI_CTX_ERR 使 ERROR 中断保持，销毁后恢复；
 *   GET_LOG 记录了错误（§6.6、§11.2）；
 * - 管理队列的 doorbell 越界：CFS = 1、HDI_FATAL = 1，控制器复位后
 *   重新使能（§5.4）；
 * - 正常关闭：等待中的 WAIT_EVENT 以 ABORTED_SHUTDOWN 完成，SHST = 10
 *   （§5.3）。
 *
 * @see spec-hdi
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/hdi.h"
#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/platform.h"
#include "bm/test.h"

/** 管理队列 */
#define ADM         (BM_DMA_BUF_BASE + 0x000000)
/** 上下文 0 的 SQ */
#define SQ0         (BM_DMA_BUF_BASE + 0x002000)
/** 上下文 0 的 CQ */
#define CQ0         (BM_DMA_BUF_BASE + 0x003000)
/** 上下文 1 的 SQ */
#define SQ1         (BM_DMA_BUF_BASE + 0x004000)
/** 上下文 1 的 CQ */
#define CQ1         (BM_DMA_BUF_BASE + 0x005000)
/** MSI-X 消息：向量 v 写到 MSG + 16 × v */
#define MSG         (BM_DMA_BUF_BASE + 0x006000)
/** TIMESTAMP 的缓冲 */
#define TSBUF       (BM_DMA_BUF_BASE + 0x007000)
/** GET_LOG、IDENTIFY 的缓冲 */
#define LOGBUF      (BM_DMA_BUF_BASE + 0x008000)
/** 上下文的区间：DPA 1 MiB 起的 1 MiB */
#define DPA_BUF     0x100000ull
/** 区间大小 */
#define RGN_SIZE    0x100000ull
/** MSI-X 表 */
#define TABLE       0x2000
/** 用于同步的事件 */
#define EVT         5
/** 时间戳的 1 us */
#define US          100ull
/**
 * 等待固件读取并开始一条命令的时间：host 看不到命令何时开始，取 NOP
 * 往返时间（约 40 us）的数倍；仿真是确定的
 */
#define START       (200 * US)

/** 设备 */
static gw100_t g;
/** HDI */
static hdi_t h;
/** 上下文 0 的队列 */
static hdi_queue_t c0;
/** 上下文 1 的队列 */
static hdi_queue_t c1;

/** 向量 v 的消息字 */
static volatile uint32_t *
msg(unsigned v)
{
    return (volatile uint32_t *)(MSG + 16 * v);
}

/** 等待向量 v 的消息到达，上限 t 个时间戳周期；超时返回 0 */
static int
wait_msg(unsigned v, uint64_t t)
{
    uint64_t t0 = gw100_timestamp(&g);
    while (*msg(v) != 0xB000u + v) {
        if (gw100_timestamp(&g) - t0 > t)
            return 0;
    }
    return 1;
}

/** 空等 t 个时间戳周期 */
static void
delay(uint64_t t)
{
    uint64_t t0 = gw100_timestamp(&g);
    while (gw100_timestamp(&g) - t0 < t)
        ;
}

/** 等待 q 的下一条 CQE 出现（不写 CQ_HEAD），上限 1 ms */
static void
reap(hdi_queue_t *q, hdi_cqe_t *e)
{
    uint64_t t0 = gw100_timestamp(&g);
    while (!hdi_reap(q, e)) {
        if (gw100_timestamp(&g) - t0 > 1000 * US)
            TEST_FAIL("等待完成记录超时");
    }
}

/** 等待 ERR_STATUS 中的位 bit 置位，上限 1 ms */
static void
wait_err(uint32_t bit)
{
    uint64_t t0 = gw100_timestamp(&g);
    while (!(bm_read32(g.bar0 + GW100_ERR_STATUS) & bit)) {
        if (gw100_timestamp(&g) - t0 > 1000 * US)
            TEST_FAIL("等待 ERR_STATUS 0x%x 超时", bit);
    }
}

/** 管理命令：创建上下文 id，向量 vec，INT_EN = ie，队列 8 个条目 */
static void
create(unsigned id, unsigned vec, unsigned ie, uintptr_t sq, uintptr_t cq,
       hdi_queue_t *q)
{
    uint32_t s[16];
    hdi_cqe_t e;
    hdi_sqe(s, HDI_OP_CREATE_CTX);
    s[10] = id | vec << 8 | ie << 24;
    s[11] = 7 | 7u << 16;
    s[12] = (uint32_t)sq;
    s[14] = (uint32_t)cq;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    hdi_queue_init(&h, q, sq, 8, cq, 8, 1 + id);
    hdi_sqe(s, HDI_OP_SET_REGION);
    s[10] = id | (HDI_PERM_R | HDI_PERM_W) << 16;
    s[12] = (uint32_t)DPA_BUF;
    s[14] = (uint32_t)RGN_SIZE;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    hdi_sqe(s, HDI_OP_SET_EVENTS);
    s[10] = id;
    s[12] = 1u << EVT;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
}

/** 构造 WAIT_EVENT：事件 evt 达到 v，超时 us 微秒（0 为无限） */
static void
wait_sqe(uint32_t s[16], unsigned evt, uint32_t v, uint32_t us)
{
    hdi_sqe(s, HDI_OP_WAIT);
    s[10] = evt;
    s[11] = v;
    s[12] = us;
}

/** 配置 MSI-X 的向量 0 – 3 并使能，同时使能 Bus Master */
static void
msix_setup(void)
{
    uintptr_t r = g.bar0;
    unsigned cap = bm_pci_find_cap(g.bdf, BM_PCI_CAP_ID_MSIX);
    TEST_ASSERT(cap != 0);
    for (unsigned v = 0; v < 4; v++) {
        bm_write32(r + TABLE + 16 * v, (uint32_t)(MSG + 16 * v));
        bm_write32(r + TABLE + 16 * v + 4, 0);
        bm_write32(r + TABLE + 16 * v + 8, 0xB000u + v);
        bm_write32(r + TABLE + 16 * v + 12, 0);
        *msg(v) = 0;
    }
    uint16_t cmd = bm_pci_cfg_read16(g.bdf, BM_PCI_COMMAND);
    bm_pci_cfg_write16(g.bdf, BM_PCI_COMMAND,
                       (uint16_t)(cmd | BM_PCI_CMD_MASTER));
    uint16_t mc = bm_pci_cfg_read16(g.bdf, cap + 2);
    bm_pci_cfg_write16(g.bdf, cap + 2, (uint16_t)(mc | 0x8000));
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    uint32_t s[16];
    hdi_cqe_t e;
    uint64_t w0;

    g = gw100_open();
    uintptr_t r = g.bar0;
    msix_setup();
    gw100_boot(&g);
    TEST_EQ(hdi_enable(&h, &g, ADM, 8, 8), 0);
    create(0, 2, 1, SQ0, CQ0, &c0);
    create(1, 3, 0, SQ1, CQ1, &c1);

    TEST_INFO("FILL 与 TIMESTAMP\n");
    hdi_sqe(s, HDI_OP_FILL);
    s[12] = (uint32_t)DPA_BUF + 4;
    s[14] = 8192 - 8;
    s[15] = 0x5A5A1234u;
    TEST_EQ(hdi_cmd(&h, &c0, s, &e), HDI_SC_SUCCESS);
    gw100_win_set(&g, 0, 0, GW100_WIN_EN | GW100_WIN_RD_ONLY);
    uintptr_t win = g.bar2 + DPA_BUF;
    TEST_EQ(bm_read32(win), 0);
    TEST_EQ(bm_read32(win + 4), 0x5A5A1234u);
    TEST_EQ(bm_read32(win + 8), 0x5A5A1234u);
    TEST_EQ(bm_read32(win + 8192 - 8), 0x5A5A1234u);
    TEST_EQ(bm_read32(win + 8192 - 4), 0);
    hdi_sqe(s, HDI_OP_TIMESTAMP);
    hdi_dptr(s, TSBUF, 8, HDI_SGL_DATA);
    uint64_t t0 = gw100_timestamp(&g);
    TEST_EQ(hdi_cmd(&h, &c0, s, &e), HDI_SC_SUCCESS);
    uint64_t t1 = gw100_timestamp(&g);
    uint64_t ts = *(volatile uint64_t *)TSBUF;
    TEST_ASSERT(ts >= t0 && ts <= t1);

    TEST_INFO("SIGNAL_EVENT 与另一个上下文的 WAIT_EVENT\n");
    wait_sqe(s, EVT, 2, 0);
    uint16_t wcid = hdi_submit(&h, &c1, s);
    hdi_sqe(s, HDI_OP_SIGNAL);
    s[10] = EVT | 1u << 8;                      /* 加 1 */
    s[11] = 1;
    TEST_EQ(hdi_cmd(&h, &c0, s, &e), HDI_SC_SUCCESS);
    delay(START);
    TEST_ASSERT(!hdi_reap(&c1, &e));            /* 计数器为 1，仍在等待 */
    TEST_EQ(hdi_cmd(&h, &c0, s, &e), HDI_SC_SUCCESS);
    TEST_EQ(hdi_wait(&h, &c1, &e, 1000 * US), 0);
    TEST_EQ(e.cid, wcid);
    TEST_EQ(e.status, HDI_SC_SUCCESS);
    wait_sqe(s, EVT, 10, 50);
    TEST_EQ(hdi_cmd(&h, &c1, s, &e), HDI_SC_TIMEOUT);
    TEST_EQ(e.dnr, 0);
    wait_sqe(s, EVT + 1, 0, 0);
    TEST_EQ(hdi_cmd(&h, &c1, s, &e), HDI_SC_ACCESS_VIOLATION);

    TEST_INFO("ABORT 中止等待中的 WAIT_EVENT\n");
    wait_sqe(s, EVT, 100, 0);
    wcid = hdi_submit(&h, &c1, s);
    delay(START);
    hdi_sqe(s, HDI_OP_ABORT);
    s[10] = 1 | (uint32_t)wcid << 16;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    TEST_EQ(e.res_lo, 1);
    TEST_EQ(hdi_wait(&h, &c1, &e, 1000 * US), 0);
    TEST_EQ(e.cid, wcid);
    TEST_EQ(e.status, HDI_SC_ABORTED_REQ);
    hdi_sqe(s, HDI_OP_ABORT);
    s[10] = 1 | (uint32_t)wcid << 16;           /* 已完成：仍然成功 */
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    TEST_EQ(e.res_lo, 0);

    TEST_INFO("中断合并：THR = 2，TIME = 2.55 ms\n");
    hdi_sqe(s, HDI_OP_SET_FEATURE);
    s[10] = 0x01;                               /* INT_COALESCE，上下文 0 */
    s[11] = 2 | 255u << 8;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    hdi_sqe(s, HDI_OP_GET_FEATURE);
    s[10] = 0x01;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    TEST_EQ(e.res_lo, 2 | 255u << 8);
    *msg(2) = 0;
    hdi_sqe(s, HDI_OP_CTX_NOP);
    hdi_submit(&h, &c0, s);
    reap(&c0, &e);
    delay(100 * US);
    TEST_EQ(*msg(2), 0);                        /* N = 1 < THR */
    hdi_sqe(s, HDI_OP_CTX_NOP);
    hdi_submit(&h, &c0, s);
    reap(&c0, &e);
    TEST_ASSERT(wait_msg(2, 100 * US));         /* N = 2 */
    hdi_cq_doorbell(&c0);
    *msg(2) = 0;
    hdi_sqe(s, HDI_OP_CTX_NOP);
    hdi_submit(&h, &c0, s);
    reap(&c0, &e);
    t0 = gw100_timestamp(&g);
    TEST_ASSERT(wait_msg(2, 4000 * US));        /* TIME 到期 */
    t1 = gw100_timestamp(&g);
    TEST_ASSERT(t1 - t0 > 2000 * US);
    hdi_cq_doorbell(&c0);
    *msg(2) = 0;
    hdi_sqe(s, HDI_OP_COPY_H2D);                /* LENGTH 为 0：错误 */
    hdi_submit(&h, &c0, s);
    reap(&c0, &e);
    TEST_EQ(e.status, HDI_SC_INVALID_FIELD);
    TEST_ASSERT(wait_msg(2, 100 * US));         /* 不受合并限制 */
    hdi_cq_doorbell(&c0);

    TEST_INFO("doorbell 写入不存在的上下文\n");
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), 0);
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ERROR);
    *msg(1) = 0;
    bm_write32(r + GW100_DB(1 + 5) + HDI_DB_SQ_TAIL, 1);
    wait_err(1u << 4);                          /* DB_INVALID */
    TEST_ASSERT(wait_msg(1, 100 * US));
    bm_write32(r + GW100_ERR_STATUS, 1u << 4);
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ERROR);
    TEST_EQ(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ERROR, 0);

    TEST_INFO("上下文 1 的 doorbell 越界：进入错误状态\n");
    *msg(1) = 0;
    bm_write32(c1.db + HDI_DB_SQ_TAIL, 100);
    wait_err(1u << 4);
    TEST_ASSERT(wait_msg(1, 100 * US));
    /* 规格不规定 DB_INVALID 与 HDI_CTX_ERR 的先后，轮询后者 */
    w0 = gw100_timestamp(&g);
    while (hdi_read(&h, HDI_CTX_ERR) != 1u << 1) {
        if (gw100_timestamp(&g) - w0 > 1000 * US)
            TEST_FAIL("等待 HDI_CTX_ERR 超时");
    }
    bm_write32(r + GW100_ERR_STATUS, 1u << 4);
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ERROR);
    /* HDI_CTX_ERR 仍不为 0：ERROR 立即重新置位 */
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ERROR);
    hdi_sqe(s, HDI_OP_IDENTIFY);
    s[10] = 1u << 8 | 1;                        /* CNS = 1，上下文 1 */
    hdi_dptr(s, LOGBUF, 4096, HDI_SGL_DATA);
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    TEST_EQ(*(volatile uint8_t *)(LOGBUF + 0), 1);
    TEST_EQ(*(volatile uint8_t *)(LOGBUF + 1), 2);  /* STATE = 错误 */
    hdi_sqe(s, HDI_OP_GET_LOG);
    s[10] = 1;
    s[11] = 128;
    hdi_dptr(s, LOGBUF, 128, HDI_SGL_DATA);
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    TEST_EQ(*(volatile uint8_t *)(LOGBUF + 8), 1);  /* 最新：上下文 1 */
    TEST_EQ(*(volatile uint8_t *)(LOGBUF + 10), HDI_SC_ABORTED_CTX);
    TEST_EQ(*(volatile uint8_t *)(LOGBUF + 64 + 10), HDI_SC_INVALID_FIELD);
    hdi_sqe(s, HDI_OP_DESTROY_CTX);
    s[10] = 1;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    TEST_EQ(hdi_read(&h, HDI_CTX_ERR), 0);
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ERROR);
    TEST_EQ(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ERROR, 0);

    TEST_INFO("管理队列的 doorbell 越界：致命错误，控制器复位\n");
    bm_write32(h.admin.db + HDI_DB_SQ_TAIL, 200);
    wait_err(1u << 4);
    w0 = gw100_timestamp(&g);
    while (!(hdi_read(&h, HDI_CSTS) & HDI_CSTS_CFS)) {
        if (gw100_timestamp(&g) - w0 > 1000 * US)
            TEST_FAIL("等待 CFS 超时");
    }
    TEST_EQ(hdi_read(&h, HDI_FATAL), 1);
    TEST_EQ(hdi_cc_write(&h, 0), 0);            /* RDY = CFS = 0 */
    TEST_EQ(hdi_read(&h, HDI_FATAL), 0);
    bm_write32(r + GW100_ERR_STATUS, 1u << 4);
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ERROR | GW100_INT_ADMIN |
                                     (1u << 16));
    TEST_EQ(bm_read32(r + GW100_INT_STATUS) & 0x10003u, 0);
    TEST_EQ(hdi_enable(&h, &g, ADM, 8, 8), 0);
    hdi_sqe(s, HDI_OP_DESTROY_CTX);             /* 复位销毁了上下文 */
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_INVALID_CONTEXT);

    TEST_INFO("正常关闭：等待中的 WAIT_EVENT 以 ABORTED_SHUTDOWN 完成\n");
    create(0, 2, 1, SQ0, CQ0, &c0);
    wait_sqe(s, EVT, 1, 0);
    wcid = hdi_submit(&h, &c0, s);
    delay(START);  /* 尚未读取的命令在关闭时不执行，不写完成记录 */
    int64_t st = hdi_cc_write(&h, HDI_CC_EN | HDI_CC_SHN(1));
    TEST_ASSERT(st >= 0 && HDI_CSTS_SHST(st) >= 1);
    w0 = gw100_timestamp(&g);
    while (HDI_CSTS_SHST(hdi_read(&h, HDI_CSTS)) != 2) {
        if (gw100_timestamp(&g) - w0 > 1000 * US)
            TEST_FAIL("等待 SHST = 10 超时");
    }
    TEST_EQ(hdi_wait(&h, &c0, &e, 1000 * US), 0);
    TEST_EQ(e.cid, wcid);
    TEST_EQ(e.status, 0x0D);                    /* ABORTED_SHUTDOWN */
    TEST_EQ(hdi_cc_write(&h, 0), 0);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), 0);
    return 0;
}
