/**
 * @file  sram_ml_read.c
 * @brief ML 描述符 UE 读取的 REJECT 与 HDMA 错误阶段
 * @see spec-sram, spec-mgmt-link
 */
#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 检查描述符 UE 读取在真实 ML 路径上的测试结果。 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uint32_t size = (uint32_t)(gw100_fw_sram_image_end - gw100_fw_sram_image);
    TEST_EQ(gw100_load_firmware(&g, gw100_fw_sram_image, size), 0);
    TEST_EQ(bm_read32(g.bar0 + GW100_FW_VERSION), 0x5A4D0008u);
    return 0;
}
