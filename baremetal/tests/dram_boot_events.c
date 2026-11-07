/**
 * @file  dram_boot_events.c
 * @brief DRAM 初始化期间的 FLR 与 D3hot/D0
 *
 * @see spec-dram, spec-mgmt-tile, spec-clock-reset-power
 */
#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/platform.h"
#include "bm/test.h"

/** PM Control/Status 配置偏移 */
#define PMCSR 0x44
/** Device Control 配置偏移 */
#define DEVCTL 0x58
/** 自检 D3hot 中断门控的 host 消息位置 */
#define MESSAGE (BM_DMA_BUF_BASE + 0x8000)

/** D3hot 中配置待发 MSI-X，确认 FLR 完成仍受电源门控 */
static void
pending_interrupt(const gw100_t *g)
{
    bm_write32(MESSAGE, 0);
    bm_write32(g->bar0 + 0x2000, MESSAGE);
    bm_write32(g->bar0 + 0x2004, 0);
    bm_write32(g->bar0 + 0x2008, 0xC0FFEE);
    bm_write32(g->bar0 + 0x200C, 0);
    unsigned cap = bm_pci_find_cap(g->bdf, BM_PCI_CAP_ID_MSIX);
    TEST_ASSERT(cap);
    bm_pci_cfg_write16(g->bdf, cap + 2, 0x8000);
    uint16_t command = bm_pci_cfg_read16(g->bdf, BM_PCI_COMMAND);
    bm_pci_cfg_write16(g->bdf, BM_PCI_COMMAND, command | BM_PCI_CMD_MASTER);
    bm_write32(g->bar0 + GW100_INT_MASK, 0);
    bm_write32(g->bar0 + GW100_INT_SET, GW100_INT_ADMIN);
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uint32_t size = (uint32_t)(gw100_fw_image_end - gw100_fw_image);
    TEST_EQ(gw100_start_firmware(&g, gw100_fw_image, size), 0);
    uint64_t start = gw100_timestamp(&g);
    for (;;) {
        uint32_t status = bm_read32(g.bar0 + GW100_BOOT_STATUS);
        if (GW100_BOOT_STAGE(status) == 4) {
            TEST_EQ(status & GW100_BOOT_FW_READY, 0);
            break;
        }
        TEST_ASSERT(gw100_timestamp(&g) - start < 100000000ull);
    }
    uint16_t control = bm_pci_cfg_read16(g.bdf, DEVCTL);
    bm_pci_cfg_write16(g.bdf, DEVCTL, control | 0x8000);
    g = gw100_open();
    TEST_EQ(bm_read32(g.bar0 + GW100_BOOT_STATUS) & GW100_BOOT_FW_READY, 0);
    bm_pci_cfg_write16(g.bdf, PMCSR, 3);
    pending_interrupt(&g);
    start = gw100_timestamp(&g);
    while (gw100_timestamp(&g) - start < 200000) {
        bm_relax();
    }
    TEST_EQ(bm_read32(g.bar0 + GW100_BOOT_STATUS) & GW100_BOOT_FW_READY, 0);
    TEST_EQ(bm_read32(MESSAGE), 0);
    bm_pci_cfg_write16(g.bdf, PMCSR, 0);
    g = gw100_open();
    gw100_boot(&g);
    TEST_ASSERT(gw100_timestamp(&g) - start < 10000000ull);
    TEST_EQ(bm_read32(g.bar0 + GW100_FW_VERSION), 0x10000);
    gw100_win_set(&g, 0, 0, GW100_WIN_EN);
    bm_write64(g.bar2 + 0x12340, 0xF1F2F3F4F5F6F7F8ull);
    TEST_EQ(bm_read64(g.bar2 + 0x12340), 0xF1F2F3F4F5F6F7F8ull);
    TEST_INFO("初始化中的 FLR 清理与电源保持通过\n");
    return 0;
}
