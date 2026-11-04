/**
 * @file  msix_its.c
 * @brief MSI-X 经 GICv3 ITS 交付给 host CPU：中断服务程序被调用
 *
 * host 初始化 GICv3 与 ITS（lib/gic.c），把 GW100 的 DeviceID（PCIe 的
 * Requester ID）的事件 0、1、2 映射到 LPI 8192 – 8194，MSI-X 表项 0 – 2
 * 指向 GITS_TRANSLATER、数据为事件号。检查：
 * - INT_SET 的 ADMIN、ERROR 位分别经向量 0、1 产生 LPI，EL3 的 IRQ 入口
 *   调用中断服务程序，各恰好一次；
 * - 屏蔽向量 2 时 CTX[0] 的事件只置 PBA、不产生中断，解除屏蔽后中断
 *   到达、PBA 清零；
 * - 加载固件、使能 HDI 后，管理队列的 NOP 完成时固件请求的 ADMIN 中断
 *   经 ITS 到达 CPU（spec-pcie §8.4，spec-hdi §12）。
 *
 * @see spec-pcie, spec-hdi
 */

#include <stdint.h>

#include "bm/gic.h"
#include "bm/gw100.h"
#include "bm/hdi.h"
#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/platform.h"
#include "bm/test.h"

/** MSI-X 表 */
#define TABLE       0x2000
/** PBA */
#define PBA         0x3000
/** 使用的向量数 */
#define NVEC        3u
/** INT_STATUS.CTX[0]：CTX_VECTOR[0] 的复位值为向量 2 */
#define INT_CTX0    (1u << 16)
/** HDI 管理队列 */
#define ADM         (BM_DMA_BUF_BASE + 0x000000)
/** 时间戳的 1 us */
#define US          100ull

/** 设备 */
static gw100_t g;
/** 每个向量对应的 LPI 被中断服务程序处理的次数 */
static volatile uint32_t hits[NVEC];
/** 范围之外的中断号出现的次数 */
static volatile uint32_t stray;

/** 中断服务程序：按 LPI 号计数 */
static void
isr(uint32_t intid)
{
    if (intid >= BM_LPI_BASE && intid < BM_LPI_BASE + NVEC)
        hits[intid - BM_LPI_BASE]++;
    else
        stray++;
}

/** 等待向量 v 的计数达到 n，上限 t 个时间戳周期；超时返回 0 */
static int
wait_hits(unsigned v, uint32_t n, uint64_t t)
{
    uint64_t t0 = gw100_timestamp(&g);
    while (hits[v] < n) {
        if (gw100_timestamp(&g) - t0 > t)
            return 0;
    }
    return 1;
}

/** 写 MSI-X 表项 v 的 Vector Control（[0] Mask） */
static void
vec_mask(unsigned v, uint32_t m)
{
    bm_write32(g.bar0 + TABLE + 16 * v + 12, m);
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    g = gw100_open();
    uintptr_t r = g.bar0;
    uint16_t cmd = bm_pci_cfg_read16(g.bdf, BM_PCI_COMMAND);
    bm_pci_cfg_write16(g.bdf, BM_PCI_COMMAND,
                       (uint16_t)(cmd | BM_PCI_CMD_MASTER));

    TEST_INFO("初始化 GICv3 与 ITS\n");
    bm_gic_init(isr);
    TEST_EQ(bm_its_init(), 0);
    uint32_t rid = (uint32_t)g.bdf.bus << 8 | (uint32_t)g.bdf.dev << 3 |
                   g.bdf.fn;
    for (unsigned v = 0; v < NVEC; v++)
        TEST_EQ(bm_its_map(rid, v, BM_LPI_BASE + v), 0);

    TEST_INFO("MSI-X 表项指向 GITS_TRANSLATER\n");
    unsigned cap = bm_pci_find_cap(g.bdf, BM_PCI_CAP_ID_MSIX);
    TEST_ASSERT(cap != 0);
    for (unsigned v = 0; v < NVEC; v++) {
        bm_write32(r + TABLE + 16 * v, (uint32_t)BM_GITS_TRANSLATER);
        bm_write32(r + TABLE + 16 * v + 4,
                   (uint32_t)((uint64_t)BM_GITS_TRANSLATER >> 32));
        bm_write32(r + TABLE + 16 * v + 8, v);      /* EventID */
        vec_mask(v, 0);
    }
    uint16_t mc = bm_pci_cfg_read16(g.bdf, cap + 2);
    bm_pci_cfg_write16(g.bdf, cap + 2, (uint16_t)(mc | 0x8000));

    TEST_INFO("INT_SET 的 ADMIN、ERROR：向量 0、1 的 LPI\n");
    bm_write32(r + GW100_INT_SET, GW100_INT_ADMIN);
    TEST_ASSERT(wait_hits(0, 1, 100 * US));
    bm_write32(r + GW100_INT_SET, GW100_INT_ERROR);
    TEST_ASSERT(wait_hits(1, 1, 100 * US));
    TEST_EQ(hits[0], 1);
    TEST_EQ(hits[2], 0);
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ADMIN | GW100_INT_ERROR);

    TEST_INFO("屏蔽向量 2：只置 PBA；解除屏蔽后中断到达\n");
    vec_mask(2, 1);
    bm_write32(r + GW100_INT_SET, INT_CTX0);
    uint64_t t0 = gw100_timestamp(&g);
    while (gw100_timestamp(&g) - t0 < 20 * US)
        ;
    TEST_EQ(hits[2], 0);
    TEST_EQ(bm_read32(r + PBA) & (1u << 2), 1u << 2);
    vec_mask(2, 0);
    TEST_ASSERT(wait_hits(2, 1, 100 * US));
    TEST_EQ(bm_read32(r + PBA) & (1u << 2), 0);
    bm_write32(r + GW100_INT_STATUS, INT_CTX0);

    TEST_INFO("HDI：管理队列 NOP 的完成中断经 ITS 到达\n");
    gw100_boot(&g);
    hdi_t h;
    hdi_cqe_t e;
    uint32_t s[16];
    TEST_EQ(hdi_enable(&h, &g, ADM, 8, 8), 0);
    uint32_t before = hits[0];
    hdi_sqe(s, HDI_OP_NOP);
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    TEST_ASSERT(wait_hits(0, before + 1, 100 * US));
    TEST_EQ(stray, 0);
    return 0;
}
