/**
 * @file  dram_access.c
 * @brief 八通道双 rank 的容量边界与 BAR2 timing 数据验证
 *
 * @see spec-dram, spec-address-map
 */
#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** 被测设备 */
static gw100_t g;

/** 通过窗口取得一个全局 DPA 的 CPU 地址 */
static uintptr_t
map(uint64_t address)
{
    uint64_t base = address & ~(uint64_t)(GW100_BAR2_WIN_SIZE - 1);
    gw100_win_set(&g, 0, base, GW100_WIN_EN);
    return g.bar2 + address - base;
}

/** 按规格插入通道位 */
static uint64_t
global(unsigned channel, uint64_t local)
{ return (local & 255) | ((uint64_t)channel << 8) | ((local >> 8) << 11); }

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    g = gw100_open();
    gw100_boot(&g);
    uint64_t address[32];
    unsigned n = 0;
    for (unsigned ch = 0; ch < 8; ++ch) {
        for (unsigned rank = 0; rank < 2; ++rank) {
            address[n++] = global(ch, (uint64_t)rank << 15);
            address[n++] = global(ch, (((4ull << 30) - 64) & ~(1ull << 15)) |
                                          ((uint64_t)rank << 15));
        }
    }
    for (unsigned pattern = 0; pattern < 2; ++pattern) {
        for (unsigned i = 0; i < n; ++i) {
            uint64_t value = address[i] ^ 0xC0FFEED0BAD00000ull;
            bm_write64(map(address[i]), pattern ? ~value : value);
        }
        for (unsigned i = 0; i < n; ++i) {
            uint64_t value = address[i] ^ 0xC0FFEED0BAD00000ull;
            TEST_EQ(bm_read64(map(address[i])), pattern ? ~value : value);
        }
    }
    uintptr_t p = map(0x200000);
    bm_write64(p, 0xFFFFFFFFFFFFFFFFull);
    bm_write8(p + 1, 0x12);
    bm_write16(p + 2, 0x3456);
    bm_write32(p + 4, 0x789ABCDE);
    TEST_EQ(bm_read64(p), 0x789ABCDE345612FFull);
    TEST_EQ(bm_read32(g.bar0 + GW100_ERR_STATUS), 0);
    TEST_INFO("八通道、十六 rank 及 32 GiB 末端数据校验通过\n");
    return 0;
}
