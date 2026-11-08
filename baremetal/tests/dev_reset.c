/**
 * @file  dev_reset.c
 * @brief 设备完全复位：BOOT_CTRL.DEV_RESET 复位功能逻辑和管理 CPU
 *
 * 固件运行时写 DEV_RESET：配置空间和 BAR 保留，host 无须重新枚举；
 * BAR0 的寄存器、窗口和 FW_VERSION 恢复复位值；管理 CPU 从启动 ROM
 * 重新开始，100 ms 内回到 STAGE 2，可以重新加载固件（spec-pcie
 * §13.1，spec-clock-reset-power §4.3、T3）。
 *
 * @see spec-pcie, spec-clock-reset-power
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** BOOT_CTRL.DEV_RESET */
#define BOOT_CTRL_DEV_RESET  (1u << 1)
/** clock.dev_reset_time = 100 ms，以设备时间戳（100 MHz）计 */
#define DEV_RESET_TIME       10000000ull

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uintptr_t r = g.bar0;
    gw100_boot(&g);
    uint32_t version = bm_read32(r + GW100_DEV_VERSION);

    TEST_INFO("弄脏寄存器后写 DEV_RESET\n");
    bm_write32(r + GW100_SCRATCH, 0x12345678u);
    bm_write32(r + GW100_INT_MASK, 0);
    gw100_win_set(&g, 0, 0x0, GW100_WIN_EN);
    uint64_t t0 = gw100_timestamp(&g);
    bm_write32(r + GW100_BOOT_CTRL, BOOT_CTRL_DEV_RESET);

    /* 无论读到的是 reset shim 还是复位后的值，下面都成立 */
    TEST_EQ(bm_read32(r + GW100_DEV_VERSION), version);
    TEST_EQ(bm_read32(r + GW100_SCRATCH), 0);
    TEST_EQ(bm_read32(r + GW100_BOOT_STATUS), 0);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), 0);

    /* 驱动在 STAGE 变为 2 之前只应读取 BOOT_STATUS（reset shim 期间
     * 时间戳读为 0）；卡住时由仿真的 tick 上限结束测试。时间戳不随
     * 设备完全复位清零，回到 STAGE 2 后再读 */
    TEST_INFO("等待回到 STAGE 2\n");
    while (GW100_BOOT_STAGE(bm_read32(r + GW100_BOOT_STATUS)) != 2)
        bm_relax();
    uint64_t dt = gw100_timestamp(&g) - t0;
    TEST_INFO("从 DEV_RESET 到 STAGE 2：%llu us\n",
              (unsigned long long)(dt / 100));
    TEST_ASSERT(dt <= DEV_RESET_TIME);

    /* 配置空间和 BAR 保留：不重新打开设备即可访问；寄存器为复位值 */
    TEST_EQ(bm_read32(r + GW100_SCRATCH), 0);
    TEST_EQ(bm_read32(r + GW100_INT_MASK), 0xFFFFFFFFu);
    TEST_EQ(bm_read32(r + GW100_INT_STATUS), 0);
    TEST_EQ(bm_read32(r + GW100_WIN_BASE(0) + GW100_WIN_CTRL), 0);
    TEST_EQ(bm_read32(r + GW100_FW_VERSION), 0);
    TEST_EQ(bm_read32(r + GW100_DEV_STATUS) & GW100_DEV_STATUS_FATAL, 0);

    TEST_INFO("重新加载固件\n");
    gw100_boot(&g);
    TEST_EQ(bm_read32(r + GW100_FW_VERSION), 0x00010000);
    return 0;
}
