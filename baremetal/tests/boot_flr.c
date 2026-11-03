/**
 * @file  boot_flr.c
 * @brief 启动期间和固件运行期间的 FLR
 *
 * 启动期间的 FLR：已写入一半的镜像作废，FLR 确认之后回到 STAGE 2，
 * 可以重新加载；固件运行期间的 FLR：FW_READY 清零，固件清理并确认后
 * 重新置位，FW_VERSION 保留（spec-pcie §13.2、spec-mgmt-tile §9）。
 *
 * @see spec-pcie, spec-mgmt-tile
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/test.h"

/** Device Control 的偏移（PCI Express capability 在 0x50） */
#define DEVCTL          (0x50 + 0x08)
/** Device Control 中的 Initiate FLR 位 */
#define DEVCTL_FLR      (1u << 15)

/** 发起一次 FLR，返回重新打开的设备 */
static gw100_t
do_flr(const gw100_t *g)
{
    uint16_t devctl = bm_pci_cfg_read16(g->bdf, DEVCTL);
    bm_pci_cfg_write16(g->bdf, DEVCTL, (uint16_t)(devctl | DEVCTL_FLR));
    return gw100_open();
}

/** 等待 BOOT_STATUS 满足 (st & mask) == want，超时 1 s */
static uint32_t
wait_status(const gw100_t *g, uint32_t mask, uint32_t want)
{
    uint64_t t0 = gw100_timestamp(g);
    for (;;) {
        uint32_t st = bm_read32(g->bar0 + GW100_BOOT_STATUS);
        if ((st & mask) == want)
            return st;
        if (gw100_timestamp(g) - t0 > 100000000ull)
            TEST_FAIL("等待 BOOT_STATUS 超时，当前 0x%08x", st);
    }
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uint32_t size = (uint32_t)(gw100_fw_image_end - gw100_fw_image);

    TEST_INFO("启动期间：写入一半镜像后 FLR\n");
    wait_status(&g, 0xF, 2);
    for (uint32_t off = 0; off < size / 2; off += 8)
        bm_write64(g.bar2 + off, 0x0123456789ABCDEFull);
    bm_dsb();
    bm_write32(g.bar0 + GW100_FW_SIZE, size);
    g = do_flr(&g);
    uint32_t st = bm_read32(g.bar0 + GW100_BOOT_STATUS);
    TEST_ASSERT(GW100_BOOT_STAGE(st) == 1 || GW100_BOOT_STAGE(st) == 2);
    TEST_EQ(st & GW100_BOOT_FW_READY, 0);
    st = wait_status(&g, 0xF, 2);
    TEST_EQ(GW100_BOOT_ERROR(st), 0);
    TEST_EQ(bm_read32(g.bar0 + GW100_FW_SIZE), 0);
    gw100_boot(&g);
    TEST_EQ(GW100_BOOT_STAGE(bm_read32(g.bar0 + GW100_BOOT_STATUS)), 4);

    TEST_INFO("固件运行期间 FLR\n");
    g = do_flr(&g);
    /* FLR 清零 FW_READY；固件清理并确认后重新置位。重新打开设备（分配
     * BAR）比固件处理 FLR 慢，这里只检查最终恢复，不检查中间的 0 */
    st = wait_status(&g, GW100_BOOT_FW_READY, GW100_BOOT_FW_READY);
    TEST_EQ(GW100_BOOT_STAGE(st), 4);
    TEST_EQ(bm_read32(g.bar0 + GW100_FW_VERSION), 0x00010000);
    TEST_ASSERT(bm_read32(g.bar0 + GW100_DEV_STATUS) &
                GW100_DEV_STATUS_DEBUG_WIN);
    TEST_EQ(bm_read32(g.bar0 + GW100_DEV_STATUS) & GW100_DEV_STATUS_FATAL,
            0);
    return 0;
}
