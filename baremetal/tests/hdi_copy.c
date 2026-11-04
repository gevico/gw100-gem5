/**
 * @file  hdi_copy.c
 * @brief HDI：控制器使能、管理命令、上下文、COPY 与完成中断
 *
 * 配置 MSI-X 并加载固件后，按驱动流程使能 HDI 控制器（spec-hdi §5.2），
 * 经管理队列执行 NOP、IDENTIFY，创建上下文 0（向量 2，CQ 4 个条目以便
 * 回绕）并设置区间；在上下文 0 上做 COPY_H2D（1 MiB + 4 KiB，固件拆成
 * 两个描述符），经 BAR2 调试窗口抽查设备内存，再 COPY_D2H 读回到连续
 * 缓冲和 SGL 的三个不连续页；检查越权、字段非法、操作码非法、SGL 长度
 * 不符的错误完成；最后销毁上下文。每条完成记录检查 CID、SQHD 和相位，
 * 并检查 MSI-X 消息与 INT_STATUS（F1、F3、F6、F7、F10、F12 的一部分）。
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

/** 管理队列：SQ、CQ 各占一个 4 KiB 区域 */
#define ADM         (BM_DMA_BUF_BASE + 0x000000)
/** 上下文 0 的 SQ */
#define SQ0         (BM_DMA_BUF_BASE + 0x002000)
/** 上下文 0 的 CQ */
#define CQ0         (BM_DMA_BUF_BASE + 0x003000)
/** IDENTIFY 的缓冲 */
#define IDBUF       (BM_DMA_BUF_BASE + 0x004000)
/** SGL 段 */
#define SGLSEG      (BM_DMA_BUF_BASE + 0x005000)
/** MSI-X 消息：向量 v 写到 MSG + 16 × v */
#define MSG         (BM_DMA_BUF_BASE + 0x006000)
/** SGL 指向的三个不连续的页，按 SGL 的顺序 */
#define PAGE0       (BM_DMA_BUF_BASE + 0x014000)
/** 第二页 */
#define PAGE1       (BM_DMA_BUF_BASE + 0x010000)
/** 第三页 */
#define PAGE2       (BM_DMA_BUF_BASE + 0x018000)
/** COPY_H2D 的源数据 */
#define SRC         (BM_DMA_BUF_BASE + 0x100000)
/** COPY_D2H 的目的缓冲 */
#define DST         (BM_DMA_BUF_BASE + 0x300000)
/** 传输长度：超过 hdi.dma_chunk（1 MiB），固件拆成两个描述符 */
#define LEN         0x101000u
/** 上下文 0 的区间：DPA 1 MiB 起的 4 MiB */
#define DPA_BUF     0x100000ull
/** 区间大小 */
#define RGN_SIZE    0x400000ull
/** MSI-X 表 */
#define TABLE       0x2000
/** INT_STATUS.CTX[0] */
#define INT_CTX0    (1u << 16)
/** 等待消息的上限：1 ms */
#define WAIT        100000ull

/** 设备 */
static gw100_t g;
/** HDI */
static hdi_t h;
/** 上下文 0 的队列 */
static hdi_queue_t c0;

/** 向量 v 的消息字 */
static volatile uint32_t *
msg(unsigned v)
{
    return (volatile uint32_t *)(MSG + 16 * v);
}

/** 等待向量 v 的消息到达；超时返回 0 */
static int
wait_msg(unsigned v)
{
    uint64_t t0 = gw100_timestamp(&g);
    while (*msg(v) != 0xB000u + v) {
        if (gw100_timestamp(&g) - t0 > WAIT)
            return 0;
    }
    return 1;
}

/** 源数据第 i 个 32 位字的值，从不为 0 */
static uint32_t
pattern(uint32_t i)
{
    return 0x9E3779B9u * (i + 1) | 1u;
}

/** 在上下文 0 上执行一条 COPY，host 侧是连续缓冲 */
static int
copy(unsigned opc, uint64_t host, uint64_t dev, uint32_t len,
     hdi_cqe_t *e)
{
    uint32_t s[16];
    hdi_sqe(s, opc);
    hdi_dptr(s, host, len, HDI_SGL_DATA);
    s[12] = (uint32_t)dev;
    s[13] = (uint32_t)(dev >> 32);
    s[14] = len;
    return hdi_cmd(&h, &c0, s, e);
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

    g = gw100_open();
    uintptr_t r = g.bar0;
    msix_setup();
    gw100_boot(&g);

    TEST_INFO("使能控制器\n");
    TEST_EQ(bm_read32(r + GW100_HDI + HDI_CAP_LO),
            4095u | 16u << 16 | 4u << 24);
    TEST_EQ(hdi_enable(&h, &g, ADM, 8, 8), 0);
    TEST_EQ(hdi_read(&h, HDI_CSTS), HDI_CSTS_RDY);

    TEST_INFO("管理队列：NOP\n");
    hdi_sqe(s, HDI_OP_NOP);
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    TEST_EQ(e.sqhd, 1);
    TEST_ASSERT(wait_msg(0));
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ADMIN);

    TEST_INFO("管理队列：IDENTIFY\n");
    for (unsigned i = 0; i < 4096; i += 4)
        *(volatile uint32_t *)(IDBUF + i) = 0xFFFFFFFFu;
    hdi_sqe(s, HDI_OP_IDENTIFY);
    hdi_dptr(s, IDBUF, 4096, HDI_SGL_DATA);
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    volatile uint8_t *id = (volatile uint8_t *)IDBUF;
    TEST_EQ(*(volatile uint16_t *)(id + 0), GW100_VENDOR_ID);
    TEST_EQ(*(volatile uint16_t *)(id + 2), GW100_DEVICE_ID);
    TEST_EQ(*(volatile uint32_t *)(id + 4), 0x100);
    TEST_EQ(id[8], 'g');
    TEST_EQ(id[13], '-');
    TEST_EQ(*(volatile uint64_t *)(id + 48), 0);
    TEST_EQ(*(volatile uint64_t *)(id + 56), (32ull << 30) - (1ull << 20));
    TEST_EQ(*(volatile uint16_t *)(id + 64), 16);
    TEST_EQ(*(volatile uint16_t *)(id + 66), 4095);
    TEST_EQ(*(volatile uint32_t *)(id + 72), 256u << 20);
    TEST_EQ(*(volatile uint64_t *)(id + 88), 0x3FF);
    TEST_EQ(*(volatile uint64_t *)(id + 96), 0x7000F);
    TEST_EQ(*(volatile uint32_t *)(id + 4092), 0);  /* 未列出的字节为 0 */

    TEST_INFO("创建上下文 0，设置区间\n");
    hdi_sqe(s, HDI_OP_CREATE_CTX);
    s[10] = 0 | 2u << 8 | 1u << 16 | 1u << 24;  /* 向量 2，INT_EN */
    s[11] = (8 - 1) | (4 - 1) << 16;
    s[12] = (uint32_t)SQ0;
    s[14] = (uint32_t)CQ0;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    hdi_queue_init(&h, &c0, SQ0, 8, CQ0, 4, 1);
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_INVALID_CONTEXT);
    hdi_sqe(s, HDI_OP_SET_REGION);
    s[10] = 0 | 0u << 8 | (HDI_PERM_R | HDI_PERM_W) << 16;
    s[12] = (uint32_t)DPA_BUF;
    s[14] = (uint32_t)RGN_SIZE;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);

    TEST_INFO("COPY_H2D：1 MiB + 4 KiB 的连续缓冲\n");
    for (uint32_t i = 0; i < LEN / 4; i += 2)
        *(volatile uint64_t *)(SRC + 4 * i) =
            pattern(i) | (uint64_t)pattern(i + 1) << 32;
    bm_dsb();
    *msg(2) = 0;
    TEST_EQ(copy(HDI_OP_COPY_H2D, SRC, DPA_BUF, LEN, &e), HDI_SC_SUCCESS);
    TEST_EQ(e.sqhd, 1);
    TEST_ASSERT(wait_msg(2));
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & INT_CTX0);
    /* 窗口 0、1 指向 DPA 0、4 MiB（WIN_BASE 须 4 MiB 对齐）：BAR2 偏移
     * 等于 DPA，覆盖整个区间 */
    gw100_win_set(&g, 0, 0, GW100_WIN_EN | GW100_WIN_RD_ONLY);
    gw100_win_set(&g, 1, GW100_BAR2_WIN_SIZE,
                  GW100_WIN_EN | GW100_WIN_RD_ONLY);
    uintptr_t win = g.bar2 + DPA_BUF;
    TEST_EQ(bm_read32(win), pattern(0));
    TEST_EQ(bm_read32(win + 0xFFFFC), pattern(0xFFFFC / 4));
    TEST_EQ(bm_read32(win + 0x100000), pattern(0x100000 / 4));
    TEST_EQ(bm_read32(win + LEN - 4), pattern(LEN / 4 - 1));
    TEST_EQ(bm_read32(win + LEN), 0);

    TEST_INFO("COPY_D2H：读回到连续缓冲\n");
    /* DST 是仿真开始后没有写过的内存，内容为 0；源数据没有为 0 的字，
     * 漏写的字比较时会被发现 */
    TEST_EQ(copy(HDI_OP_COPY_D2H, DST, DPA_BUF, LEN, &e), HDI_SC_SUCCESS);
    for (uint32_t i = 0; i < LEN; i += 8) {
        if (*(volatile uint64_t *)(DST + i) !=
            *(volatile uint64_t *)(SRC + i))
            TEST_FAIL("D2H 数据在偏移 0x%x 处不符", i);
    }
    TEST_EQ(*(volatile uint64_t *)(DST + LEN), 0);

    TEST_INFO("COPY_D2H：SGL 的三个不连续页\n");
    for (unsigned i = 0; i < 4096; i += 8) {
        *(volatile uint64_t *)(PAGE0 + i) = 0;
        *(volatile uint64_t *)(PAGE1 + i) = 0;
        *(volatile uint64_t *)(PAGE2 + i) = 0;
    }
    hdi_sgl_data(SGLSEG + 0, PAGE0, 4096);
    hdi_sgl_data(SGLSEG + 16, PAGE1, 4096);
    hdi_sgl_data(SGLSEG + 32, PAGE2, 4096);
    bm_dsb();
    hdi_sqe(s, HDI_OP_COPY_D2H);
    hdi_dptr(s, SGLSEG, 48, HDI_SGL_SEGMENT);
    s[12] = (uint32_t)(DPA_BUF + 4096);
    s[14] = 3 * 4096;
    TEST_EQ(hdi_cmd(&h, &c0, s, &e), HDI_SC_SUCCESS);
    for (unsigned i = 0; i < 4096; i += 8) {
        TEST_EQ(*(volatile uint64_t *)(PAGE0 + i),
                *(volatile uint64_t *)(SRC + 4096 + i));
        TEST_EQ(*(volatile uint64_t *)(PAGE1 + i),
                *(volatile uint64_t *)(SRC + 8192 + i));
        TEST_EQ(*(volatile uint64_t *)(PAGE2 + i),
                *(volatile uint64_t *)(SRC + 12288 + i));
    }

    TEST_INFO("错误完成：越权、字段非法、操作码非法、SGL 长度不符\n");
    TEST_EQ(copy(HDI_OP_COPY_H2D, SRC, DPA_BUF + RGN_SIZE - 4, 8, &e),
            HDI_SC_ACCESS_VIOLATION);
    TEST_EQ(e.dnr, 1);
    TEST_EQ(bm_read32(win + RGN_SIZE - 4), 0);     /* 没有写入 */
    TEST_EQ(copy(HDI_OP_COPY_H2D, SRC, DPA_BUF, 6, &e),
            HDI_SC_INVALID_FIELD);
    hdi_sqe(s, HDI_OP_IDENTIFY);
    TEST_EQ(hdi_cmd(&h, &c0, s, &e), HDI_SC_INVALID_OPCODE);
    hdi_sqe(s, HDI_OP_COPY_H2D);
    hdi_dptr(s, SRC, 4096, HDI_SGL_DATA);
    s[12] = (uint32_t)DPA_BUF;
    s[14] = 8192;
    TEST_EQ(hdi_cmd(&h, &c0, s, &e), HDI_SC_SGL_INVALID);
    hdi_sqe(s, HDI_OP_CTX_NOP);
    s[3] = 1;                                   /* RsvdZ 不为 0 */
    TEST_EQ(hdi_cmd(&h, &c0, s, &e), HDI_SC_INVALID_FIELD);
    /* 上下文 0 的 CQ 有 4 个条目，8 条完成记录使它回绕两次 */
    TEST_EQ(c0.phase, 1);
    TEST_EQ(c0.cq_head, 0);

    TEST_INFO("销毁上下文 0\n");
    hdi_sqe(s, HDI_OP_DESTROY_CTX);
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_INVALID_CONTEXT);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), 0);
    TEST_EQ(hdi_read(&h, HDI_CTX_ERR), 0);
    return 0;
}
