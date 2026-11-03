/**
 * @file  boot_ok.c
 * @brief 上电初始化：host 经 BAR2 加载固件，ROM 校验后跳转，固件置位 FW_READY
 *
 * 走完 spec-pcie §6.4 的驱动加载流程：等待 STAGE = 2，写入镜像，写
 * FW_SIZE、FW_START，等待 FW_READY。检查启动后的状态和固件版本。
 *
 * @see spec-pcie, spec-mgmt-tile
 */

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uintptr_t r = g.bar0;

    uint64_t t0 = gw100_timestamp(&g);
    gw100_boot(&g);
    TEST_INFO("从打开设备到 FW_READY：%llu ns\n",
              (unsigned long long)((gw100_timestamp(&g) - t0) * 10));

    uint32_t st = bm_read32(r + GW100_BOOT_STATUS);
    TEST_EQ(GW100_BOOT_STAGE(st), 4);
    TEST_ASSERT(st & GW100_BOOT_FW_READY);
    TEST_EQ(GW100_BOOT_ERROR(st), 0);
    TEST_EQ(bm_read32(r + GW100_FW_VERSION), 0x00010000);
    TEST_EQ(bm_read32(r + GW100_DEV_STATUS) & GW100_DEV_STATUS_FATAL, 0);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), 0);
    return 0;
}
