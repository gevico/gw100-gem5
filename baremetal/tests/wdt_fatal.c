/**
 * @file  wdt_fatal.c
 * @brief 看门狗第二级超时：管理 CPU 停住，HIF 置位 DEV_FATAL
 *
 * 加载看门狗的测试固件（fw_wdt.img）。它在初始化中自检第一级超时、
 * PLIC 源 7 和合法喂狗（失败则 STAGE = 0xF），置位 FW_READY 后不再
 * 喂狗，只把递增的心跳写入 FW_VERSION。检查：第二级超时后 DEV_FATAL
 * 与 INT_STATUS.ERROR 置位；心跳停止，即 CPU 已停住；FLR 不清除
 * DEV_FATAL，停住的固件也不再置位 FW_READY；设备完全复位清除
 * DEV_FATAL，停住的 CPU 从启动 ROM 重新开始，可以重新加载固件
 * （spec-mgmt-tile §7.4、spec-pcie §6.2、§8.4、§13.1）。
 *
 * @see spec-mgmt-tile, spec-pcie
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
/** 等待 DEV_FATAL 的上限：测试固件两个超时周期是 4 ms，取 20 ms */
#define FATAL_WAIT      2000000ull
/** 1 ms，以设备时间戳（100 MHz）计 */
#define MS              100000ull
/** BOOT_CTRL.DEV_RESET */
#define BOOT_CTRL_DEV_RESET  (1u << 1)

/** 等待 n 个设备时间戳计数 */
static void
wait_ts(const gw100_t *g, uint64_t n)
{
    uint64_t t0 = gw100_timestamp(g);
    while (gw100_timestamp(g) - t0 < n)
        bm_relax();
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    uint32_t size = (uint32_t)(gw100_fw_wdt_image_end - gw100_fw_wdt_image);
    TEST_EQ(gw100_load_firmware(&g, gw100_fw_wdt_image, size), 0);
    uint64_t ready = gw100_timestamp(&g);
    uintptr_t r = g.bar0;
    TEST_EQ(bm_read32(r + GW100_DEV_STATUS) & GW100_DEV_STATUS_FATAL, 0);

    TEST_INFO("心跳在走\n");
    uint32_t h0 = bm_read32(r + GW100_FW_VERSION);
    wait_ts(&g, MS / 10);
    TEST_NE(bm_read32(r + GW100_FW_VERSION), h0);

    TEST_INFO("第二级超时置位 DEV_FATAL 与 INT_STATUS.ERROR\n");
    while (!(bm_read32(r + GW100_DEV_STATUS) & GW100_DEV_STATUS_FATAL)) {
        if (gw100_timestamp(&g) - ready > FATAL_WAIT)
            TEST_FAIL("FW_READY 之后 20 ms 内没有置位 DEV_FATAL");
        bm_relax();
    }
    TEST_INFO("DEV_FATAL 在 FW_READY 之后 %llu us 置位\n",
              (unsigned long long)((gw100_timestamp(&g) - ready) / 100));
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ERROR);

    TEST_INFO("管理 CPU 已停住：心跳不再变化\n");
    wait_ts(&g, MS / 10);   /* 等停机前已发出的心跳写完 */
    uint32_t h = bm_read32(r + GW100_FW_VERSION);
    wait_ts(&g, MS);
    TEST_EQ(bm_read32(r + GW100_FW_VERSION), h);

    TEST_INFO("FLR 不清除 DEV_FATAL，固件不再置位 FW_READY\n");
    uint16_t devctl = bm_pci_cfg_read16(g.bdf, DEVCTL);
    bm_pci_cfg_write16(g.bdf, DEVCTL, (uint16_t)(devctl | DEVCTL_FLR));
    g = gw100_open();
    r = g.bar0;
    TEST_ASSERT(bm_read32(r + GW100_DEV_STATUS) & GW100_DEV_STATUS_FATAL);
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ERROR);
    wait_ts(&g, MS);
    TEST_EQ(bm_read32(r + GW100_BOOT_STATUS) & GW100_BOOT_FW_READY, 0);

    TEST_INFO("设备完全复位后恢复\n");
    uint64_t t0 = gw100_timestamp(&g);
    bm_write32(r + GW100_BOOT_CTRL, BOOT_CTRL_DEV_RESET);
    /* STAGE 变为 2 之前只读 BOOT_STATUS（见 dev_reset.c） */
    while (GW100_BOOT_STAGE(bm_read32(r + GW100_BOOT_STATUS)) != 2)
        bm_relax();
    TEST_ASSERT(gw100_timestamp(&g) - t0 <= 100 * MS);
    TEST_EQ(bm_read32(r + GW100_DEV_STATUS) & GW100_DEV_STATUS_FATAL, 0);
    TEST_EQ(bm_read32(r + GW100_INT_STATUS), 0);
    gw100_boot(&g);
    return 0;
}
