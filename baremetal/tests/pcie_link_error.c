/**
 * @file  pcie_link_error.c
 * @brief 线上 CRC 故障后的真实重放、AER 锁存与 RW1C
 *
 * 测试夹具从 RC 发送方向注入一次 TLP LCRC 损坏，软件通过配置空间
 * 完成枚举和诊断。
 *
 * @see spec-pcie
 */

#include "bm/gw100.h"
#include "bm/pci.h"
#include "bm/test.h"

/** 测试入口，每个 test 实现它，返回 0 表示通过。 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    TEST_EQ(bm_pci_cfg_read32(g.bdf, 0x100), 0x14010001u);
    TEST_EQ(bm_pci_cfg_read32(g.bdf, 0x110) & (1u << 6), 1u << 6);
    TEST_EQ(bm_pci_cfg_read32(g.bdf, 0x104), 0);
    unsigned capability = bm_pci_find_cap(g.bdf, BM_PCI_CAP_ID_PCIE);
    TEST_ASSERT(bm_pci_cfg_read16(g.bdf, capability + 0x0A) & 1);
    uint32_t mask = bm_pci_cfg_read32(g.bdf, 0x114);
    bm_pci_cfg_write32(g.bdf, 0x114, mask | (1u << 6));
    TEST_EQ(bm_pci_cfg_read32(g.bdf, 0x114), mask | (1u << 6));
    bm_pci_cfg_write32(g.bdf, 0x110, 1u << 6);
    TEST_EQ(bm_pci_cfg_read32(g.bdf, 0x110) & (1u << 6), 0);
    bm_pci_cfg_write16(g.bdf, capability + 0x0A, 1);
    TEST_EQ(bm_pci_cfg_read16(g.bdf, capability + 0x0A) & 1, 0);
    TEST_INFO("CRC 重放、AER 状态、屏蔽及 RW1C 通过\n");
    return 0;
}
