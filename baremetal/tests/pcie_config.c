/**
 * @file  pcie_config.c
 * @brief 配置空间：ID、BAR 类型与大小、capability 链及其字段
 *
 * 对应 spec-pcie 验收项 F1–F3。F3 只验证 Command 寄存器本身：gem5 的
 * Simple CPU 在访问未解码地址时直接 panic，无法从测试里观察"关闭内存
 * 解码后设备不再响应"。
 *
 * @see spec-pcie
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/pci.h"
#include "bm/test.h"

/**
 * 配置空间头部：ID、Class、Header Type、Interrupt Pin、Capabilities List 位
 */
static void
check_header(bm_pci_bdf_t d)
{
    TEST_EQ(bm_pci_cfg_read16(d, BM_PCI_VENDOR_ID), GW100_VENDOR_ID);
    TEST_EQ(bm_pci_cfg_read16(d, BM_PCI_DEVICE_ID), GW100_DEVICE_ID);
    TEST_EQ(bm_pci_cfg_read8(d, BM_PCI_CLASS_BASE), GW100_CLASS_BASE);
    TEST_EQ(bm_pci_cfg_read8(d, BM_PCI_CLASS_SUB), 0);
    TEST_EQ(bm_pci_cfg_read8(d, BM_PCI_CLASS_PROG), 0);
    TEST_EQ(bm_pci_cfg_read8(d, BM_PCI_HEADER_TYPE), 0);
    TEST_EQ(bm_pci_cfg_read8(d, BM_PCI_INTERRUPT_PIN), 1);  /* INTA */
    TEST_ASSERT(bm_pci_cfg_read16(d, BM_PCI_STATUS) & BM_PCI_STATUS_CAP_LIST);
}

/**
 * BAR 的大小与类型：BAR0 1 MiB 32 位不可预取，BAR2 64 MiB 64 位可预取，
 * 其余未实现
 */
static void
check_bars(bm_pci_bdf_t d)
{
    bm_pci_bar_t b0 = bm_pci_bar_probe(d, 0);
    TEST_EQ(b0.size, GW100_BAR0_SIZE);
    TEST_EQ(b0.is_io, 0);
    TEST_EQ(b0.is_64bit, 0);
    TEST_EQ(b0.prefetchable, 0);

    TEST_EQ(bm_pci_bar_probe(d, 1).size, 0);    /* BAR1 未实现 */

    bm_pci_bar_t b2 = bm_pci_bar_probe(d, 2);
    TEST_EQ(b2.size, GW100_BAR2_SIZE);
    TEST_EQ(b2.is_io, 0);
    TEST_EQ(b2.is_64bit, 1);
    TEST_EQ(b2.prefetchable, 1);

    TEST_EQ(bm_pci_bar_probe(d, 4).size, 0);    /* BAR4/5 未实现 */
}

/** PM capability：位置、版本、D0、No_Soft_Reset，以及不支持 D1 */
static void
check_pm(bm_pci_bdf_t d)
{
    unsigned pm = bm_pci_find_cap(d, BM_PCI_CAP_ID_PM);
    TEST_EQ(pm, 0x40);
    TEST_EQ(bm_pci_cfg_read16(d, pm + 2) & 0x7, 3);     /* PM 版本 3 */

    uint16_t pmcsr = bm_pci_cfg_read16(d, pm + 4);
    TEST_EQ(pmcsr & 0x3, 0);                            /* D0 */
    TEST_EQ(pmcsr & (1u << 3), 0);                      /* No_Soft_Reset = 0 */

    /* 不支持 D1：写入被忽略 */
    bm_pci_cfg_write16(d, pm + 4, 1);
    TEST_EQ(bm_pci_cfg_read16(d, pm + 4) & 0x3, 0);
}

/** PCI Express capability 的各字段，以及链表的结尾和扩展配置空间 */
static void
check_pcie(bm_pci_bdf_t d)
{
    unsigned x = bm_pci_find_cap(d, BM_PCI_CAP_ID_PCIE);
    TEST_EQ(x, 0x50);

    uint16_t caps = bm_pci_cfg_read16(d, x + 0x02);
    TEST_EQ(caps & 0xF, 2);                     /* 版本 2 */
    TEST_EQ((caps >> 4) & 0xF, 0);              /* PCI Express Endpoint */

    uint32_t devcap = bm_pci_cfg_read32(d, x + 0x04);
    TEST_EQ(devcap & 0x7, 1);                   /* MPS Supported = 256 B */
    TEST_ASSERT(devcap & (1u << 5));            /* Extended Tag */
    TEST_ASSERT(devcap & (1u << 28));           /* FLR Capable */

    uint16_t devctl = bm_pci_cfg_read16(d, x + 0x08);
    TEST_EQ((devctl >> 12) & 0x7, 2);           /* MRRS 默认 512 B */
    TEST_ASSERT(devctl & (1u << 8));            /* Extended Tag Enable */
    TEST_EQ(devctl & (1u << 15), 0);            /* Initiate FLR 读回 0 */

    /* MPS 可写 */
    uint16_t mps256 = (uint16_t)((devctl & ~(7u << 5)) | (1u << 5));
    bm_pci_cfg_write16(d, x + 0x08, mps256);
    TEST_EQ((bm_pci_cfg_read16(d, x + 0x08) >> 5) & 0x7, 1);

    uint32_t linkcap = bm_pci_cfg_read32(d, x + 0x0C);
    TEST_EQ(linkcap & 0xF, 4);                  /* 最高 Gen4 */
    TEST_EQ((linkcap >> 4) & 0x3F, 4);          /* 最大 x4 */
    TEST_EQ((linkcap >> 10) & 0x3, 2);          /* ASPM 只支持 L1 */

    /* Link Capabilities 只读 */
    bm_pci_cfg_write32(d, x + 0x0C, 0);
    TEST_EQ(bm_pci_cfg_read32(d, x + 0x0C), linkcap);

    uint16_t linksta = bm_pci_cfg_read16(d, x + 0x12);
    TEST_EQ(linksta & 0xF, 4);                  /* 当前 Gen4 */
    TEST_EQ((linksta >> 4) & 0x3F, 4);          /* 当前 x4 */

    uint32_t devcap2 = bm_pci_cfg_read32(d, x + 0x24);
    TEST_EQ(devcap2 & 0xF, 0x3);                /* 完成超时范围 A、B */
    TEST_EQ(devcap2 & (1u << 4), 0);            /* 不支持关闭完成超时 */

    TEST_EQ(bm_pci_cfg_read32(d, x + 0x2C) & 0xFE, 0x1E);  /* 2.5–16 GT/s */

    /* 链表到此结束；本阶段没有 MSI、MSI-X */
    TEST_EQ(bm_pci_cfg_read8(d, x + 1), 0);
    TEST_EQ(bm_pci_find_cap(d, BM_PCI_CAP_ID_MSI), 0);
    TEST_EQ(bm_pci_find_cap(d, BM_PCI_CAP_ID_MSIX), 0);

    /* 扩展配置空间：没有扩展 capability */
    TEST_EQ(bm_pci_cfg_read32(d, 0x100), 0);
}

/** Command 寄存器：清除 Memory Space 后该位读回 0，BAR 的值保持不变 */
static void
check_command(void)
{
    gw100_t g = gw100_open();
    uint32_t bar0 = bm_pci_cfg_read32(g.bdf, BM_PCI_BAR0);

    TEST_ASSERT(bm_pci_cfg_read16(g.bdf, BM_PCI_COMMAND) & BM_PCI_CMD_MEM);
    bm_pci_cfg_write16(g.bdf, BM_PCI_COMMAND, 0);
    TEST_EQ(bm_pci_cfg_read16(g.bdf, BM_PCI_COMMAND) & BM_PCI_CMD_MEM, 0);
    TEST_EQ(bm_pci_cfg_read32(g.bdf, BM_PCI_BAR0), bar0);   /* BAR 保持 */
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    bm_pci_bdf_t d;
    TEST_EQ(bm_pci_find(GW100_VENDOR_ID, GW100_DEVICE_ID, &d), 0);
    TEST_INFO("GW100 位于 %02x:%02x.%u\n", d.bus, d.dev, d.fn);

    check_header(d);
    check_bars(d);
    check_pm(d);
    check_pcie(d);
    check_command();
    return 0;
}
