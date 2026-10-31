/**
 * @file  pcie_flr.c
 * @brief FLR 与 D3hot→D0 复位：配置空间、BAR、寄存器复位，设备内存保留
 *
 * 对应 spec-pcie 验收项 F11、F12 中寄存器层的部分（本阶段还没有 DMA）。
 *
 * @see spec-pcie
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** PM capability 的偏移 */
#define PM_CAP          0x40
/** PCI Express capability 的偏移 */
#define PCIE_CAP        0x50
/** Device Control 的偏移 */
#define DEVCTL          (PCIE_CAP + 0x08)
/** Device Control 中的 Initiate FLR 位 */
#define DEVCTL_FLR      (1u << 15)
/** Device Control 的复位值：RO、ExtTag、NoSnoop 使能，MRRS 512 B */
#define DEVCTL_DEFAULT  0x2910

/** 改动各处状态，并经窗口写入一个可在复位后检查的数据 */
static void
dirty(gw100_t *g)
{
    bm_write32(g->bar0 + GW100_SCRATCH, 0xA5A5A5A5u);
    bm_write32(g->bar0 + GW100_INT_MASK, 0);
    bm_write32(g->bar0 + 0x040, 0);     /* 制造一个错误 */
    gw100_win_set(g, 0, 0x0, GW100_WIN_EN);
    bm_write32(g->bar2 + 0x200, 0xFEEDFACEu);
    /* MPS 改为 256 B */
    bm_pci_cfg_write16(g->bdf, DEVCTL, DEVCTL_DEFAULT | (1u << 5));
}

/** 复位之后：配置空间回到初值，重新打开后寄存器为复位值、设备内存保留 */
static void
check_after_reset(bm_pci_bdf_t d)
{
    TEST_EQ(bm_pci_cfg_read16(d, BM_PCI_COMMAND), 0);
    TEST_EQ(bm_pci_cfg_read32(d, BM_PCI_BAR0), 0);
    TEST_EQ(bm_pci_cfg_read32(d, BM_PCI_BAR0 + 8), 0xC);   /* 64 位、可预取 */
    TEST_EQ(bm_pci_cfg_read32(d, BM_PCI_BAR0 + 12), 0);
    TEST_EQ(bm_pci_cfg_read16(d, DEVCTL), DEVCTL_DEFAULT);
    TEST_EQ(bm_pci_cfg_read16(d, PM_CAP + 4) & 0x3, 0);

    gw100_t g = gw100_open();
    TEST_EQ(bm_read32(g.bar0 + GW100_SCRATCH), 0);
    TEST_EQ(bm_read32(g.bar0 + GW100_INT_MASK), 0xFFFFFFFFu);
    TEST_EQ(bm_read32(g.bar0 + GW100_ERR_STATUS), 0);
    TEST_EQ(bm_read32(g.bar0 + GW100_INT_STATUS), 0);
    TEST_EQ(bm_read32(g.bar0 + GW100_WIN_BASE(0) + GW100_WIN_CTRL), 0);

    gw100_win_set(&g, 0, 0x0, GW100_WIN_EN);
    TEST_EQ(bm_read32(g.bar2 + 0x200), 0xFEEDFACEu);
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();

    TEST_INFO("FLR\n");
    dirty(&g);
    TEST_ASSERT(bm_read32(g.bar0 + GW100_ERR_STATUS) != 0);
    uint16_t devctl = bm_pci_cfg_read16(g.bdf, DEVCTL);
    bm_pci_cfg_write16(g.bdf, DEVCTL, (uint16_t)(devctl | DEVCTL_FLR));
    check_after_reset(g.bdf);

    TEST_INFO("D3hot → D0\n");
    g = gw100_open();
    dirty(&g);
    bm_pci_cfg_write16(g.bdf, PM_CAP + 4, 3);
    TEST_EQ(bm_pci_cfg_read16(g.bdf, PM_CAP + 4) & 0x3, 3);
    bm_pci_cfg_write16(g.bdf, PM_CAP + 4, 0);
    check_after_reset(g.bdf);
    return 0;
}
