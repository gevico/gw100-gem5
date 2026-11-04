/**
 * @file  boot_errors.c
 * @brief 启动失败的路径：镜像校验失败、镜像大小非法、启动窗口的非法写入
 *
 * 每种失败都应回到 STAGE 2 并报告对应的 ERROR（spec-pcie §6.4），随后
 * 可以重新加载成功；非法写入还要记录 ERR_STATUS.BOOT_WIN（§7.2）。
 *
 * @see spec-pcie, spec-mgmt-tile
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 镜像的副本，用于制造错误 */
static uint8_t buf[4096] __attribute__((aligned(8)));

/** BOOT_STATUS 的 ERROR 编码：FW_SIZE 非法 */
#define ERR_SIZE        1
/** ERROR 编码：本次加载期间启动窗口收到过非法写入 */
#define ERR_WIN_WRITE   2
/** ERROR 编码：镜像校验失败 */
#define ERR_VERIFY      3

/** 等待 STAGE = 2，超时 1 s */
static void
wait_stage2(const gw100_t *g)
{
    uint64_t t0 = gw100_timestamp(g);
    while (GW100_BOOT_STAGE(bm_read32(g->bar0 + GW100_BOOT_STATUS)) != 2) {
        if (gw100_timestamp(g) - t0 > 100000000ull)
            TEST_FAIL("等待 STAGE = 2 超时");
        bm_relax();
    }
}

/**
 * 写入 size 字节的镜像，以 fw_size 作为 FW_SIZE 启动，返回结束时的
 * BOOT_STATUS（FW_READY、STAGE = 2 且 ERROR ≠ 0，或 STAGE = 0xF）
 */
static uint32_t
load(const gw100_t *g, const uint8_t *img, uint32_t size, uint32_t fw_size)
{
    wait_stage2(g);
    for (uint32_t off = 0; off < size; off += 8) {
        uint64_t v = 0;
        for (unsigned i = 0; i < 8; i++)
            v |= (uint64_t)img[off + i] << (8 * i);
        bm_write64(g->bar2 + off, v);
    }
    bm_dsb();
    bm_write32(g->bar0 + GW100_FW_SIZE, fw_size);
    bm_write32(g->bar0 + GW100_BOOT_CTRL, GW100_BOOT_FW_START);
    for (;;) {
        uint32_t st = bm_read32(g->bar0 + GW100_BOOT_STATUS);
        if ((st & GW100_BOOT_FW_READY) || GW100_BOOT_STAGE(st) == 0xF ||
            (GW100_BOOT_STAGE(st) == 2 && GW100_BOOT_ERROR(st)))
            return st;
        bm_relax();
    }
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uintptr_t r = g.bar0;
    uint32_t size = (uint32_t)(gw100_fw_image_end - gw100_fw_image);
    TEST_ASSERT(size <= sizeof(buf));
    for (uint32_t i = 0; i < size; i++)
        buf[i] = gw100_fw_image[i];

    TEST_INFO("镜像内容被改动：校验失败\n");
    buf[size - 8] ^= 0x5A;
    uint32_t st = load(&g, buf, size, size);
    TEST_EQ(GW100_BOOT_STAGE(st), 2);
    TEST_EQ(GW100_BOOT_ERROR(st), ERR_VERIFY);
    TEST_EQ(bm_read32(r + GW100_FW_SIZE), 0);   /* ROM 清零了镜像大小 */
    buf[size - 8] ^= 0x5A;

    TEST_INFO("FW_SIZE 不是 8 的倍数\n");
    st = load(&g, buf, size, size + 4);
    TEST_EQ(GW100_BOOT_STAGE(st), 2);
    TEST_EQ(GW100_BOOT_ERROR(st), ERR_SIZE);

    TEST_INFO("启动窗口收到 32 位写入\n");
    wait_stage2(&g);
    bm_write32(r + GW100_ERR_STATUS, 0xFFFFFFFFu);
    bm_write32(g.bar2 + 0x10, 0x12345678u);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), GW100_ERR_BOOT_WIN);
    TEST_EQ(bm_read32(r + GW100_ERR_ADDR), 0x10);
    TEST_EQ(bm_read32(g.bar2), 0xFFFFFFFFu);    /* 启动窗口读回全 1 */
    st = load(&g, buf, size, size);
    TEST_EQ(GW100_BOOT_STAGE(st), 2);
    TEST_EQ(GW100_BOOT_ERROR(st), ERR_WIN_WRITE);
    bm_write32(r + GW100_ERR_STATUS, 0xFFFFFFFFu);

    TEST_INFO("重新加载成功\n");
    gw100_boot(&g);
    st = bm_read32(r + GW100_BOOT_STATUS);
    TEST_EQ(GW100_BOOT_STAGE(st), 4);
    TEST_EQ(GW100_BOOT_ERROR(st), 0);
    return 0;
}
