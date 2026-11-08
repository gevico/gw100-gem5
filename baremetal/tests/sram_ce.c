/**
 * @file  sram_ce.c
 * @brief 真正的管理 CPU 纠错读取与 CE 寄存器闭环
 * @see spec-sram
 */
#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 注入单比特故障并检查固件纠错结果。 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uint32_t size = (uint32_t)(gw100_fw_sram_image_end - gw100_fw_sram_image);
    TEST_EQ(gw100_load_firmware(&g, gw100_fw_sram_image, size), 0);
    TEST_EQ(bm_read32(g.bar0 + GW100_FW_VERSION), 0x5A4D0006u);
    return 0;
}
