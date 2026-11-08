/**
 * @file  pcie_link.c
 * @brief 数字链路配置、真实启动和 Host BAR 往返延迟观测
 *
 * Host 计数器包含 CPU 与原生互连时间。PCIe 入口到完整完成的规格上限
 * 由原始链路 trace 独立核验。
 *
 * @see spec-pcie
 */

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/test.h"
#include "generated/gw100_pcie_defs.h"

/** 读 Host 架构计数器，ISB 固定指令观测边界。 */
static uint64_t
counter(void)
{
    uint64_t value;
    __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(value) : : "memory");
    return value;
}

/** 当前 Host 架构计数器频率，单位 Hz。 */
static uint64_t
frequency(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

/** 测试入口，每个 test 实现它，返回 0 表示通过。 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    unsigned capability = bm_pci_find_cap(g.bdf, BM_PCI_CAP_ID_PCIE);
    TEST_ASSERT(capability != 0);
    TEST_EQ(bm_pci_cfg_read32(g.bdf, 0x100), 0x14010001u);
    uint16_t status = bm_pci_cfg_read16(g.bdf, capability + 0x12);
    unsigned generation = status & 15;
    unsigned width = (status >> 4) & 63;
    TEST_ASSERT(generation >= 1 && generation <= 4);
    TEST_ASSERT(width == 1 || width == 2 || width == 4);
    TEST_EQ(status & ((1u << 11) | (1u << 13)), 0);
    uint16_t control = bm_pci_cfg_read16(g.bdf, capability + 8);
    control = (control & ~((7u << 5) | (7u << 12))) |
              (1u << 5) | (1u << 8) | (2u << 12);
    bm_pci_cfg_write16(g.bdf, capability + 8, control);
    TEST_EQ((bm_pci_cfg_read16(g.bdf, capability + 8) >> 5) & 7, 1);
    TEST_EQ((bm_pci_cfg_read16(g.bdf, capability + 8) >> 12) & 7, 2);
    gw100_boot(&g);
    gw100_win_set(&g, 0, 0, GW100_WIN_EN);
    for (unsigned i = 0; i < 8; ++i)
        bm_write64(g.bar2 + 0x200000 + 8 * i, 0x1122334455667788ull);
    TEST_EQ(bm_read64(g.bar2 + 0x200000), 0x1122334455667788ull);
    bm_write8(g.bar2 + 0x200003, 0xA5);
    bm_write16(g.bar2 + 0x200006, 0x5A5A);
    TEST_EQ(bm_read64(g.bar2 + 0x200000), 0x5A5A3344A5667788ull);
    bm_write32(g.bar0 + GW100_SCRATCH, 0xC0FFEE12);
    TEST_EQ(bm_read32(g.bar0 + GW100_SCRATCH), 0xC0FFEE12);
    uint64_t begin = counter();
    uint32_t scratch = bm_read32(g.bar0 + GW100_SCRATCH);
    uint64_t bar0 = counter() - begin;
    begin = counter();
    uint64_t data = bm_read64(g.bar2 + 0x200000);
    uint64_t bar2 = counter() - begin;
    TEST_EQ(scratch, 0xC0FFEE12);
    TEST_EQ(data, 0x5A5A3344A5667788ull);
    TEST_EQ(bm_pci_cfg_read32(g.bdf, 0x104), 0);
    TEST_EQ(bm_pci_cfg_read32(g.bdf, 0x110), 0);
    TEST_INFO("PCIe Gen%u x%u MPS=256 MRRS=512 Host counter=%llu Hz\n",
              generation, width, (unsigned long long)frequency());
    TEST_INFO("Host BAR0 RTT=%llu ns BAR2 RTT=%llu ns\n",
              (unsigned long long)((bar0 * 1000000000ull + frequency() - 1) /
                                   frequency()),
              (unsigned long long)((bar2 * 1000000000ull + frequency() - 1) /
                                   frequency()));
    TEST_EQ(GW_PCIE_TICK_PS, 1);
    return 0;
}
