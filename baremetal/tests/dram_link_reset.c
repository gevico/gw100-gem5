/**
 * @file  dram_link_reset.c
 * @brief 外部冷复位、热复位和链路断开后的重新枚举与内存训练
 *
 * semihosting 只向仿真夹具请求数字复位输入，重新启动、配置和训练
 * 由真实 ROM 与固件执行。
 * @see spec-dram, spec-clock-reset-power
 */
#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 向仿真夹具报告一个外部数字复位通知，然后继续执行 */
static void
reset_input(unsigned cause)
{
    unsigned long block[2] = {0x20025, cause};
    register unsigned long x0 __asm__("x0") = 0x18;
    register const void *x1 __asm__("x1") = block;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    /* 独立于设备时间戳，给同步器和复位保持期留出时间。 */
    for (unsigned i = 0; i < 4; ++i) {
        bm_relax();
    }
}

/** 测试入口，每个 test 实现它，返回 0 表示通过 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    gw100_boot(&g);
    for (unsigned cause = 2; cause <= 8; cause <<= 1) {
        uint64_t before = gw100_timestamp(&g);
        bm_write32(g.bar0 + GW100_SCRATCH, 0xABCD1234);
        reset_input(cause);
        TEST_EQ(bm_pci_cfg_read16(g.bdf, BM_PCI_COMMAND), 0);
        g = gw100_open();
        TEST_ASSERT(gw100_timestamp(&g) < before);
        TEST_EQ(bm_read32(g.bar0 + GW100_SCRATCH), 0);
        TEST_EQ(bm_read32(g.bar0 + GW100_FW_VERSION), 0);
        TEST_EQ(bm_read32(g.bar0 + GW100_BOOT_STATUS) & GW100_BOOT_FW_READY,
                0);
        gw100_boot(&g);
        gw100_win_set(&g, 0, 0, GW100_WIN_EN);
        bm_write64(g.bar2 + 0x30000, cause);
        TEST_EQ(bm_read64(g.bar2 + 0x30000), cause);
    }
    TEST_INFO("PERST、Hot Reset、Link Down 后的重新训练通过\n");
    return 0;
}
