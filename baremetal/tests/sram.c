/**
 * @file  sram.c
 * @brief 真实管理 CPU 的 SRAM 逐 bank 与部分写回归
 * @see spec-sram
 */
#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 加载公共 SRAM 测试固件并检查正常结果。 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uint32_t size = (uint32_t)(gw100_fw_sram_image_end - gw100_fw_sram_image);
    TEST_EQ(gw100_load_firmware(&g, gw100_fw_sram_image, size), 0);
    TEST_EQ(bm_read32(g.bar0 + GW100_FW_VERSION), 0x5A4D0000u);
    TEST_EQ(bm_read32(g.bar0 + GW100_DEV_STATUS) & GW100_DEV_STATUS_FATAL, 0);
    return 0;
}
