/**
 * @file  hdma_basic.c
 * @brief Host DMA：H2D、D2H（连续缓冲与 SGL）、错误停止与 RESUME
 *
 * 加载 HDMA 的测试固件（fw_hdma.img）。命令协议实现之前，固件按约定的
 * host 地址 BM_DMA_BUF_BASE 访问 host 内存，见 firmware/fw/hdma_test.c：
 * host 先准备 12 KiB 源数据和一段 SGL，固件做 H2D、两次 D2H（连续缓冲
 * 和 SGL）和一次 DEV_ADDR 越界的 H2D，把结果写入 FW_VERSION。host 检查
 * 数据、BAR0 的 HDMA 镜像（字节计数、完成数、CH_ERR）和 ERR_STATUS
 * （spec-host-dma F1、F4、F7、F9）。
 *
 * @see spec-host-dma
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/platform.h"
#include "bm/test.h"

/** 源数据 */
#define H_SRC       (BM_DMA_BUF_BASE + 0x00000)
/** 连续的目的缓冲（固件写到 +4） */
#define H_DST1      (BM_DMA_BUF_BASE + 0x10000)
/** SGL */
#define H_SGL       (BM_DMA_BUF_BASE + 0x20000)
/** SGL 指向的三个不连续的页，按 SGL 的顺序 */
#define H_PAGE0     (BM_DMA_BUF_BASE + 0x33000)
/** 第二页 */
#define H_PAGE1     (BM_DMA_BUF_BASE + 0x31000)
/** 第三页 */
#define H_PAGE2     (BM_DMA_BUF_BASE + 0x35000)
/** 测试数据的长度 */
#define LEN         12288u

/** 写一个 SGL 的 DATA 条目 */
static void
sgl_data(uintptr_t at, uint64_t addr, uint32_t len)
{
    *(volatile uint64_t *)at = addr;
    *(volatile uint32_t *)(at + 8) = len;
    *(volatile uint32_t *)(at + 12) = 0;    /* TYPE = DATA */
}

/** a 与 b 起的 n 字节是否相同 */
static int
same(uintptr_t a, uintptr_t b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 4) {
        if (*(volatile uint32_t *)(a + i) != *(volatile uint32_t *)(b + i))
            return 0;
    }
    return 1;
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    TEST_INFO("准备 host 缓冲\n");
    for (uint32_t i = 0; i < LEN; i += 4)
        *(volatile uint32_t *)(H_SRC + i) = 0xC0DE0000u + i;
    for (uint32_t i = 0; i < LEN + 8; i += 4)
        *(volatile uint32_t *)(H_DST1 + i) = 0;
    for (uint32_t i = 0; i < 4096; i += 4) {
        *(volatile uint32_t *)(H_PAGE0 + i) = 0;
        *(volatile uint32_t *)(H_PAGE1 + i) = 0;
        *(volatile uint32_t *)(H_PAGE2 + i) = 0;
    }
    sgl_data(H_SGL + 0, H_PAGE0, 4096);
    sgl_data(H_SGL + 16, H_PAGE1, 4096);
    sgl_data(H_SGL + 32, H_PAGE2, 4096);
    bm_dsb();

    gw100_t g = gw100_open();
    uintptr_t r = g.bar0;
    /* HDMA 访问 host 内存需要 Bus Master Enable */
    uint16_t cmd = bm_pci_cfg_read16(g.bdf, BM_PCI_COMMAND);
    bm_pci_cfg_write16(g.bdf, BM_PCI_COMMAND,
                       (uint16_t)(cmd | BM_PCI_CMD_MASTER));

    uint32_t size =
        (uint32_t)(gw100_fw_hdma_image_end - gw100_fw_hdma_image);
    TEST_EQ(gw100_load_firmware(&g, gw100_fw_hdma_image, size), 0);
    TEST_EQ(bm_read32(r + GW100_FW_VERSION), 0xD0A00000u);

    TEST_INFO("检查数据\n");
    TEST_ASSERT(same(H_DST1 + 4, H_SRC, LEN));
    TEST_EQ(*(volatile uint32_t *)H_DST1, 0);
    TEST_EQ(*(volatile uint32_t *)(H_DST1 + 4 + LEN), 0);
    TEST_ASSERT(same(H_PAGE0, H_SRC, 4096));
    TEST_ASSERT(same(H_PAGE1, H_SRC + 4096, 4096));
    TEST_ASSERT(same(H_PAGE2, H_SRC + 8192, 4096));

    TEST_INFO("检查 BAR0 的 HDMA 镜像\n");
    TEST_EQ(bm_read32(r + GW100_HDMA_MIRROR + 0x00C), 1);       /* EN */
    TEST_EQ(bm_read32(r + GW100_HDMA_MIRROR + 0x010), LEN + 64);
    TEST_EQ(bm_read32(r + GW100_HDMA_MIRROR + 0x018), 2 * LEN);
    TEST_EQ(bm_read32(r + GW100_HDMA_CH(1, 0x28)), 5);   /* DESC_DONE */
    TEST_EQ(bm_read32(r + GW100_HDMA_CH(1, 0x18)), 5);   /* HEAD */
    TEST_EQ(bm_read32(r + GW100_HDMA_CH(1, 0x04)) & 3, 0);
    uint32_t err = bm_read32(r + GW100_HDMA_CH(1, 0x1C));
    TEST_EQ(err & 0xFF, 5);                              /* DEV_ADDR */
    TEST_EQ(err >> 16, 3);
    /* 镜像只读：写入被忽略，不报错 */
    bm_write32(r + GW100_HDMA_MIRROR + 0x00C, 0);
    TEST_EQ(bm_read32(r + GW100_HDMA_MIRROR + 0x00C), 1);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), 0);
    return 0;
}
