/**
 * @file  pcie_enum.c
 * @brief 枚举 GW100 并访问 BAR0 寄存器
 *
 * 验证 host 到设备的 MMIO 通路：经 ECAM 找到设备、核对配置空间、
 * 探测并分配 BAR0、使能内存解码，再读写设备寄存器。
 *
 * 期望值对应当前的 Gw100PciDevice 模型（BAR0 4 KiB，寄存器 VERSION、
 * STATUS、CMD_ADDR、DOORBELL）；模型按规格改造后同步更新本测试。
 *
 * @see spec-pcie
 */

#include <stdint.h>

#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/test.h"

#define GW100_VENDOR_ID     0x1234
#define GW100_DEVICE_ID     0x1000
#define GW100_CLASS         0x12        /* Processing Accelerator */
#define GW100_BAR0_SIZE     0x1000

#define REG_VERSION         0x00
#define REG_STATUS          0x04
#define REG_CMD_ADDR        0x08        /* 64 位，只接受 8 字节写 */
#define REG_DOORBELL        0x10

int
test_main(void)
{
    bm_pci_bdf_t d;

    if (bm_pci_find(GW100_VENDOR_ID, GW100_DEVICE_ID, &d) != 0)
        TEST_FAIL("PCIe 总线上没有找到 GW100（%04x:%04x）",
                  GW100_VENDOR_ID, GW100_DEVICE_ID);
    TEST_INFO("GW100 位于 %02x:%02x.%u\n", d.bus, d.dev, d.fn);

    TEST_EQ(bm_pci_cfg_read8(d, BM_PCI_CLASS_BASE), GW100_CLASS);
    TEST_EQ(bm_pci_cfg_read8(d, BM_PCI_HEADER_TYPE) & 0x7F, 0);

    bm_pci_bar_t bar0 = bm_pci_bar_probe(d, 0);
    TEST_EQ(bar0.size, GW100_BAR0_SIZE);
    TEST_EQ(bar0.is_io, 0);
    TEST_EQ(bar0.is_64bit, 0);

    uintptr_t base = bm_pci_bar_assign(d, 0);
    TEST_NE(base, 0);
    bm_pci_enable(d, BM_PCI_CMD_MEM);
    TEST_INFO("BAR0 映射到 CPU 地址 %p\n", (void *)base);

    TEST_EQ(bm_read32(base + REG_VERSION), 1);
    TEST_EQ(bm_read32(base + REG_STATUS), 0);

    bm_write64(base + REG_CMD_ADDR, 0x1122334455667788ull);
    TEST_EQ(bm_read64(base + REG_CMD_ADDR), 0x1122334455667788ull);

    bm_write32(base + REG_DOORBELL, 0xA5A55A5Au);
    TEST_EQ(bm_read32(base + REG_DOORBELL), 0xA5A55A5Au);

    return 0;
}
