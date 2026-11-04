/**
 * @file  gw100.c
 * @brief GW100 助手函数的实现
 *
 * @see spec-pcie
 */

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/**
 * 查找 GW100，分配 BAR0、BAR2，使能内存解码。
 * 找不到设备或分配失败时直接以 FAIL 结束测试。
 */
gw100_t
gw100_open(void)
{
    gw100_t g;

    if (bm_pci_find(GW100_VENDOR_ID, GW100_DEVICE_ID, &g.bdf) != 0)
        TEST_FAIL("PCIe 总线上没有找到 GW100（%04x:%04x）",
                  GW100_VENDOR_ID, GW100_DEVICE_ID);
    g.bar0 = bm_pci_bar_assign(g.bdf, 0);
    g.bar2 = bm_pci_bar_assign(g.bdf, 2);
    if (!g.bar0 || !g.bar2)
        TEST_FAIL("BAR 分配失败：BAR0=%p BAR2=%p", (void *)g.bar0,
                  (void *)g.bar2);
    bm_pci_enable(g.bdf, BM_PCI_CMD_MEM);
    return g;
}

/**
 * 把 BAR2 的窗口 i 指向设备物理地址 base，flags 为 GW100_WIN_* 的组合；
 * 配置后读回一次，确认生效
 */
void
gw100_win_set(const gw100_t *g, unsigned i, uint64_t base, uint32_t flags)
{
    uintptr_t w = g->bar0 + GW100_WIN_BASE(i);
    bm_write32(w + GW100_WIN_BASE_LO, (uint32_t)base);
    bm_write32(w + GW100_WIN_BASE_HI, (uint32_t)(base >> 32));
    bm_write32(w + GW100_WIN_CTRL, flags);
    /* 读回一次，确认配置已生效后再访问窗口（spec-pcie §7.3） */
    (void)bm_read32(w + GW100_WIN_CTRL);
}

/** 时间戳频率：100 MHz（spec-clock-reset-power §3.2） */
#define GW100_TS_HZ 100000000ull

/** 读设备时间戳（100 MHz） */
uint64_t
gw100_timestamp(const gw100_t *g)
{
    uint32_t lo = bm_read32(g->bar0 + GW100_TIMESTAMP_LO);
    uint32_t hi = bm_read32(g->bar0 + GW100_TIMESTAMP_HI);
    return ((uint64_t)hi << 32) | lo;
}

/**
 * 按 spec-pcie §6.4 的驱动流程加载 size 字节的固件镜像 img。成功返回
 * 0；加载失败（STAGE 回到 2）返回 ERROR 编码；启动失败（STAGE = 0xF）
 * 返回 -1；等待超时返回 -2
 */
int
gw100_load_firmware(const gw100_t *g, const uint8_t *img, uint32_t size)
{
    /* 第 1 步：等待 STAGE = 2，超时 1 s；固件已在运行时跳过加载 */
    uint64_t t0 = gw100_timestamp(g);
    for (;;) {
        uint32_t st = bm_read32(g->bar0 + GW100_BOOT_STATUS);
        if (GW100_BOOT_STAGE(st) == 4 && (st & GW100_BOOT_FW_READY))
            return 0;
        if (GW100_BOOT_STAGE(st) == 2)
            break;
        if (gw100_timestamp(g) - t0 > GW100_TS_HZ)
            return -2;
        bm_relax();
    }
    /* 第 2、3 步：镜像长度已是 8 的倍数（tools/fwpack.py），64 位写入 */
    if (size > bm_read32(g->bar0 + GW100_FW_LOAD_MAX) || size % 8)
        return -2;
    for (uint32_t off = 0; off < size; off += 8) {
        uint64_t v = 0;
        for (unsigned i = 0; i < 8; i++)
            v |= (uint64_t)img[off + i] << (8 * i);
        bm_write64(g->bar2 + off, v);
    }
    /* 第 4 步：写屏障，再写 FW_SIZE、FW_START */
    bm_dsb();
    bm_write32(g->bar0 + GW100_FW_SIZE, size);
    bm_write32(g->bar0 + GW100_BOOT_CTRL, GW100_BOOT_FW_START);
    /* 第 5 步：轮询 BOOT_STATUS，超时 5 s */
    t0 = gw100_timestamp(g);
    for (;;) {
        uint32_t st = bm_read32(g->bar0 + GW100_BOOT_STATUS);
        if (st & GW100_BOOT_FW_READY)
            return 0;
        if (GW100_BOOT_STAGE(st) == 2 && GW100_BOOT_ERROR(st))
            return (int)GW100_BOOT_ERROR(st);
        if (GW100_BOOT_STAGE(st) == 0xF)
            return -1;
        if (gw100_timestamp(g) - t0 > 5 * GW100_TS_HZ)
            return -2;
        bm_relax();
    }
}

/** 加载嵌入的固件镜像，失败时测试失败 */
void
gw100_boot(const gw100_t *g)
{
    uint32_t size = (uint32_t)(gw100_fw_image_end - gw100_fw_image);
    int r = gw100_load_firmware(g, gw100_fw_image, size);
    if (r != 0)
        TEST_FAIL("固件加载失败：%d", r);
    TEST_INFO("固件已就绪，FW_VERSION = 0x%08x\n",
              bm_read32(g->bar0 + GW100_FW_VERSION));
}
