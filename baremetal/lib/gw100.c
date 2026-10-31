/**
 * @file  gw100.c
 * @brief GW100 助手函数的实现
 *
 * @see spec-pcie
 */

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/**
 * 查找 GW100，分配 BAR0、BAR2，使能内存解码。
 * 找不到设备或分配失败时直接以 FAIL 结束测试。
 */
gw100_t
gw100_open(void)
{
    gw100_t g;

    if (bm_pci_find(GW100_VENDOR_ID, GW100_DEVICE_ID, &g.bdf) != 0)
        TEST_FAIL("PCIe 总线上没有找到 GW100（%04x:%04x）",
                  GW100_VENDOR_ID, GW100_DEVICE_ID);
    g.bar0 = bm_pci_bar_assign(g.bdf, 0);
    g.bar2 = bm_pci_bar_assign(g.bdf, 2);
    if (!g.bar0 || !g.bar2)
        TEST_FAIL("BAR 分配失败：BAR0=%p BAR2=%p", (void *)g.bar0,
                  (void *)g.bar2);
    bm_pci_enable(g.bdf, BM_PCI_CMD_MEM);
    return g;
}

/**
 * 把 BAR2 的窗口 i 指向设备物理地址 base，flags 为 GW100_WIN_* 的组合；
 * 配置后读回一次，确认生效
 */
void
gw100_win_set(const gw100_t *g, unsigned i, uint64_t base, uint32_t flags)
{
    uintptr_t w = g->bar0 + GW100_WIN_BASE(i);
    bm_write32(w + GW100_WIN_BASE_LO, (uint32_t)base);
    bm_write32(w + GW100_WIN_BASE_HI, (uint32_t)(base >> 32));
    bm_write32(w + GW100_WIN_CTRL, flags);
    /* 读回一次，确认配置已生效后再访问窗口（spec-pcie §7.3） */
    (void)bm_read32(w + GW100_WIN_CTRL);
}
