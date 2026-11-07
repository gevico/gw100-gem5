/**
 * @file  dram_error.c
 * @brief 真实固件对控制器训练失败、超时及自检失败的处理
 *
 * 默认测试配置注入通道 0 rank 0 的训练失败。
 * @see spec-dram, spec-mgmt-tile
 */
#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uint32_t size = (uint32_t)(gw100_fw_image_end - gw100_fw_image);
    TEST_EQ(gw100_load_firmware(&g, gw100_fw_image, size), -1);
    uint32_t status = bm_read32(g.bar0 + GW100_BOOT_STATUS);
    TEST_EQ(GW100_BOOT_STAGE(status), 0xF);
    TEST_EQ(GW100_BOOT_ERROR(status), 4);
    TEST_EQ(status & GW100_BOOT_FW_READY, 0);
    uint32_t diagnostic = bm_read32(g.bar0 + GW100_FW_VERSION);
    TEST_EQ(diagnostic >> 24, 0xD2);
    TEST_ASSERT((diagnostic & 0xFF) != 0);
    TEST_INFO("DRAM 启动失败快照 0x%08x\n", diagnostic);
    return 0;
}
