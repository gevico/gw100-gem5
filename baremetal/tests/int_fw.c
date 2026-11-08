/**
 * @file  int_fw.c
 * @brief 固件的中断请求：CTX_VECTOR、INT_LEVEL、INT_REQ
 *
 * host 先配置并使能 MSI-X，再加载中断请求的测试固件（fw_int.img）。
 * 固件把上下文 3 映射到向量 5、置位 INT_LEVEL.ADMIN，然后请求 ADMIN、
 * FW_EVENT、CTX[3]。检查：向量 0 和 5 收到消息；INT_STATUS 的三位置位；
 * host 清零 ADMIN 时因 INT_LEVEL 仍为 1 而重新置位，并再发一条消息；
 * 清零其余两位后它们保持为 0（spec-pcie §6.5、§8.4）。
 *
 * @see spec-pcie
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/platform.h"
#include "bm/test.h"

/** 消息写到这里：向量 v 在 MSG + 16 × v */
#define MSG         (BM_DMA_BUF_BASE + 0x50000)
/** MSI-X 表 */
#define TABLE       0x2000
/** INT_STATUS.CTX[3] */
#define INT_CTX3    (1u << 19)
/** 等待消息的上限：100 us */
#define WAIT        10000ull

/** 设备 */
static gw100_t g;

/** 向量 v 的消息字 */
static volatile uint32_t *
msg(unsigned v)
{
    return (volatile uint32_t *)(MSG + 16 * v);
}

/** 等待向量 v 的消息到达；超时返回 0 */
static int
wait_msg(unsigned v)
{
    uint64_t t0 = gw100_timestamp(&g);
    while (*msg(v) != 0xB000u + v) {
        if (gw100_timestamp(&g) - t0 > WAIT)
            return 0;
    }
    return 1;
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    g = gw100_open();
    uintptr_t r = g.bar0;
    unsigned cap = bm_pci_find_cap(g.bdf, BM_PCI_CAP_ID_MSIX);
    TEST_ASSERT(cap != 0);
    for (unsigned v = 0; v < 6; v++) {
        bm_write32(r + TABLE + 16 * v, (uint32_t)(MSG + 16 * v));
        bm_write32(r + TABLE + 16 * v + 4, 0);
        bm_write32(r + TABLE + 16 * v + 8, 0xB000u + v);
        bm_write32(r + TABLE + 16 * v + 12, 0);
        *msg(v) = 0;
    }
    uint16_t cmd = bm_pci_cfg_read16(g.bdf, BM_PCI_COMMAND);
    bm_pci_cfg_write16(g.bdf, BM_PCI_COMMAND,
                       (uint16_t)(cmd | BM_PCI_CMD_MASTER));
    uint16_t mc = bm_pci_cfg_read16(g.bdf, cap + 2);
    bm_pci_cfg_write16(g.bdf, cap + 2, (uint16_t)(mc | 0x8000));

    uint32_t size = (uint32_t)(gw100_fw_int_image_end - gw100_fw_int_image);
    TEST_EQ(gw100_load_firmware(&g, gw100_fw_int_image, size), 0);
    TEST_EQ(bm_read32(r + GW100_FW_VERSION), 0x1A700000u);

    TEST_INFO("向量 0、5 收到消息\n");
    TEST_ASSERT(wait_msg(0));
    TEST_ASSERT(wait_msg(5));
    TEST_EQ(*msg(2), 0);                     /* CTX[3] 不再用默认向量 */
    uint32_t want = GW100_INT_ADMIN | GW100_INT_FW_EVENT | INT_CTX3;
    TEST_EQ(bm_read32(r + GW100_INT_STATUS) & want, want);

    TEST_INFO("INT_LEVEL 使 ADMIN 清零后重新置位\n");
    *msg(0) = 0;
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ADMIN);
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ADMIN);
    TEST_ASSERT(wait_msg(0));
    bm_write32(r + GW100_INT_STATUS, GW100_INT_FW_EVENT | INT_CTX3);
    TEST_EQ(bm_read32(r + GW100_INT_STATUS) &
            (GW100_INT_FW_EVENT | INT_CTX3), 0);
    return 0;
}
