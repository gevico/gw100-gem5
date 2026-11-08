/**
 * @file  pcie_checkpoint.c
 * @brief 实际 gem5 排空后恢复 PCIe L0/L1、配置与 MSI-X
 *
 * @see spec-pcie
 */

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/test.h"

/** 请求夹具保存 checkpoint，恢复后从下一条指令继续。 */
static void
checkpoint(unsigned stage)
{
    unsigned long block[2] = {0x20025, stage};
    register unsigned long x0 __asm__("x0") = 0x18;
    register const void *x1 __asm__("x1") = block;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
}

/** Host 内部计数器等待 50 us，期间不发送 PCIe 请求。 */
static void
idle(void)
{
    uint64_t start, now, frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(start));
    do {
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(now));
    } while (now - start < frequency / 20000);
}

/** 验证恢复后配置、寄存器与 MSI-X 表保持。 */
static void
verify(gw100_t *device)
{
    TEST_EQ(bm_read32(device->bar0 + GW100_SCRATCH), 0xa5c0ffee);
    TEST_EQ(bm_read32(device->bar0 + 0x2008), 0x12345678);
    TEST_EQ((bm_pci_cfg_read16(device->bdf, 0x58) >> 5) & 7, 1);
}

/** 测试入口，每个 test 实现它，返回 0 表示通过。 */
int
test_main(void)
{
    gw100_t device = gw100_open();
    uint16_t control = bm_pci_cfg_read16(device.bdf, 0x58);
    bm_pci_cfg_write16(device.bdf, 0x58, (control & ~0xe0u) | 0x20u);
    bm_write32(device.bar0 + GW100_SCRATCH, 0xa5c0ffee);
    bm_write32(device.bar0 + 0x2008, 0x12345678);
    verify(&device);
    checkpoint(0x100);
    verify(&device);
    bm_pci_cfg_write16(device.bdf, 0x60, 2);
    idle();
    checkpoint(0x101);
    verify(&device);
    TEST_INFO("L0/L1 checkpoint 恢复后的链路和寄存器检查通过\n");
    return 0;
}
