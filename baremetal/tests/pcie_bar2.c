/**
 * @file  pcie_bar2.c
 * @brief BAR2 窗口：配置、经窗口读写设备内存、错误情形
 *
 * 对应 spec-pcie 验收项 F7。设备内存为 32 GiB，起始设备物理地址 0。
 *
 * @see spec-pcie
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 设备内存大小：32 GiB */
#define DEVMEM_SIZE     0x800000000ull

/** 打开后的 GW100 */
static gw100_t g;

/** 窗口 i 在 BAR2 中的 CPU 地址 */
static uintptr_t
win(unsigned i)
{
    return g.bar2 + i * GW100_BAR2_WIN_SIZE;
}

/** 窗口 i 的配置寄存器 field 在 BAR0 中的 CPU 地址 */
static uintptr_t
win_reg(unsigned i, unsigned field)
{
    return g.bar0 + GW100_WIN_BASE(i) + field;
}

/** 清零 ERR_STATUS 和 INT_STATUS */
static void
clear_errors(void)
{
    bm_write32(g.bar0 + GW100_ERR_STATUS, 0xFFFFFFFFu);
    bm_write32(g.bar0 + GW100_INT_STATUS, 0xFFFFFFFFu);
}

/** 窗口配置寄存器：复位值、对齐与位宽掩码、保留字、64 位写 */
static void
check_registers(void)
{
    /* 复位后全部窗口未使能 */
    for (unsigned i = 0; i < GW100_BAR2_WIN_COUNT; i++)
        TEST_EQ(bm_read32(win_reg(i, GW100_WIN_CTRL)), 0);

    /* 基址按 4 MiB 对齐，高位只保留 40 位物理地址的部分；CTRL 只有两位 */
    bm_write32(win_reg(7, GW100_WIN_BASE_LO), 0xFFFFFFFFu);
    bm_write32(win_reg(7, GW100_WIN_BASE_HI), 0xFFFFFFFFu);
    bm_write32(win_reg(7, GW100_WIN_CTRL), 0xFFFFFFFFu);
    TEST_EQ(bm_read32(win_reg(7, GW100_WIN_BASE_LO)), 0xFFC00000u);
    TEST_EQ(bm_read32(win_reg(7, GW100_WIN_BASE_HI)), 0xFFu);
    TEST_EQ(bm_read32(win_reg(7, GW100_WIN_CTRL)),
            GW100_WIN_EN | GW100_WIN_RD_ONLY);
    TEST_EQ(bm_read32(win_reg(7, 0x4)), 0);     /* 保留字 */

    /* 64 位写一次更新 BASE_LO 和 BASE_HI */
    bm_write64(win_reg(7, GW100_WIN_BASE_LO), 0x0000001234C00000ull);
    TEST_EQ(bm_read64(win_reg(7, GW100_WIN_BASE_LO)), 0x0000001234C00000ull);
    bm_write32(win_reg(7, GW100_WIN_CTRL), 0);
}

/** 经窗口读写设备内存：各种宽度、多窗口别名、互不影响、移动窗口、内存末尾 */
static void
check_data(void)
{
    gw100_win_set(&g, 0, 0x0, GW100_WIN_EN);

    /* 1、2、4、8 字节读写 */
    bm_write8(win(0) + 0x10, 0x5A);
    TEST_EQ(bm_read8(win(0) + 0x10), 0x5A);
    bm_write16(win(0) + 0x20, 0xBEEF);
    TEST_EQ(bm_read16(win(0) + 0x20), 0xBEEF);
    bm_write32(win(0) + 0x30, 0xCAFEF00Du);
    TEST_EQ(bm_read32(win(0) + 0x30), 0xCAFEF00Du);
    bm_write64(win(0) + 0x40, 0x0123456789ABCDEFull);
    TEST_EQ(bm_read64(win(0) + 0x40), 0x0123456789ABCDEFull);
    TEST_EQ(bm_read32(win(0) + 0x44), 0x01234567u);    /* 小端序 */

    /* 两个窗口指向同一处：经窗口 1 写入的数据，经窗口 0 可见 */
    gw100_win_set(&g, 1, 0x0, GW100_WIN_EN);
    bm_write32(win(1) + 0x100, 0x11112222u);
    TEST_EQ(bm_read32(win(0) + 0x100), 0x11112222u);

    /* 指向不同地址的窗口互不影响 */
    gw100_win_set(&g, 2, 0x10000000ull, GW100_WIN_EN);
    bm_write32(win(2) + 0x100, 0x33334444u);
    TEST_EQ(bm_read32(win(0) + 0x100), 0x11112222u);
    TEST_EQ(bm_read32(win(2) + 0x100), 0x33334444u);

    /* 移动窗口 0：看到新位置的数据；移回后原数据仍在 */
    gw100_win_set(&g, 0, 0x10000000ull, GW100_WIN_EN);
    TEST_EQ(bm_read32(win(0) + 0x100), 0x33334444u);
    gw100_win_set(&g, 0, 0x0, GW100_WIN_EN);
    TEST_EQ(bm_read32(win(0) + 0x100), 0x11112222u);

    /* 设备内存的最后 4 MiB */
    gw100_win_set(&g, 15, DEVMEM_SIZE - GW100_BAR2_WIN_SIZE, GW100_WIN_EN);
    bm_write32(win(15) + GW100_BAR2_WIN_SIZE - 4, 0x77778888u);
    TEST_EQ(bm_read32(win(15) + GW100_BAR2_WIN_SIZE - 4), 0x77778888u);
}

/** 错误情形：未使能、目标超出设备内存、写只读窗口 */
static void
check_errors(void)
{
    uintptr_t r = g.bar0;

    /* 未使能的窗口：读回全 1，写入丢弃 */
    clear_errors();
    TEST_EQ(bm_read32(win(3) + 0x8), 0xFFFFFFFFu);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), GW100_ERR_WIN_DISABLED);
    TEST_EQ(bm_read32(r + GW100_ERR_ADDR), 3 * GW100_BAR2_WIN_SIZE + 0x8);
    TEST_EQ(GW100_ERR_INFO_BAR(bm_read32(r + GW100_ERR_INFO)), 2);
    bm_write32(win(3) + 0x8, 0x12345678u);
    gw100_win_set(&g, 3, 0x0, GW100_WIN_EN);
    TEST_NE(bm_read32(win(3) + 0x8), 0x12345678u);

    /* 目标超出设备内存 */
    clear_errors();
    gw100_win_set(&g, 4, DEVMEM_SIZE, GW100_WIN_EN);
    TEST_EQ(bm_read32(win(4)), 0xFFFFFFFFu);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), GW100_ERR_WIN_TARGET);

    /* 只读窗口：写入丢弃并报错，读正常 */
    clear_errors();
    gw100_win_set(&g, 5, 0x0, GW100_WIN_EN | GW100_WIN_RD_ONLY);
    bm_write32(win(5) + 0x100, 0x99999999u);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), GW100_ERR_WIN_TARGET);
    TEST_EQ(bm_read32(win(5) + 0x100), 0x11112222u);
    clear_errors();
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    g = gw100_open();
    TEST_INFO("BAR0 位于 %p，BAR2 位于 %p\n", (void *)g.bar0, (void *)g.bar2);

    check_registers();
    check_data();
    check_errors();
    return 0;
}
