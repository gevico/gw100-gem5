/**
 * @file  sram_ue_store.c
 * @brief 管理 CPU 的 UE 访问错误、mcause 与 mtval 验证
 * @see spec-sram
 */
#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 检查 RMW 写 UE 异常对应的失败阶段和测试标记。 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uint32_t size = (uint32_t)(gw100_fw_sram_image_end - gw100_fw_sram_image);
    TEST_EQ(gw100_start_firmware(&g, gw100_fw_sram_image, size), 0);
    uint64_t start = gw100_timestamp(&g);
    while (bm_read32(g.bar0 + GW100_FW_VERSION) != 0x5A4DF007u) {
        TEST_ASSERT(gw100_timestamp(&g) - start < 100000000ull);
        bm_relax();
    }
    uint32_t status = bm_read32(g.bar0 + GW100_BOOT_STATUS);
    TEST_EQ(GW100_BOOT_STAGE(status), 0xf);
    TEST_EQ(status & GW100_BOOT_FW_READY, 0);
    return 0;
}
