/**
 * @file  pcie_regs.c
 * @brief BAR0 全局寄存器、访问规则与错误记录、启动寄存器
 *
 * 对应 spec-pcie 验收项 F4–F6。
 *
 * @see spec-pcie
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/test.h"

/** BAR0 的 CPU 地址 */
static uintptr_t r;

/** 清零 ERR_STATUS 和 INT_STATUS，并确认已清零 */
static void
clear_errors(void)
{
    bm_write32(r + GW100_ERR_STATUS, 0xFFFFFFFFu);
    bm_write32(r + GW100_INT_STATUS, 0xFFFFFFFFu);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), 0);
    TEST_EQ(bm_read32(r + GW100_INT_STATUS), 0);
}

/** DEV_VERSION、DEV_CAPS、DEV_STATUS；只读寄存器忽略写入；1、2 字节读 */
static void
check_identity(void)
{
    TEST_EQ(bm_read32(r + GW100_DEV_VERSION), 0x00000100);
    /* 16 个窗口、32 个 MSI-X 向量、16 个上下文、8 个 DMA 通道 */
    TEST_EQ(bm_read32(r + GW100_DEV_CAPS),
            16u | (32u << 8) | (16u << 16) | (8u << 24));
    uint32_t st = bm_read32(r + GW100_DEV_STATUS);
    TEST_EQ(st & 0xF, 4);               /* Gen4 */
    TEST_EQ((st >> 4) & 0x1F, 4);       /* x4 */
    /* 桩模型相当于固件已开启调试窗口 */
    TEST_ASSERT(st & GW100_DEV_STATUS_DEBUG_WIN);

    /* 只读寄存器的写入被忽略，且不算错误 */
    bm_write32(r + GW100_DEV_VERSION, 0xDEADBEEF);
    TEST_EQ(bm_read32(r + GW100_DEV_VERSION), 0x00000100);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), 0);

    /* 1、2 字节读返回对应的字节 */
    TEST_EQ(bm_read16(r + GW100_DEV_CAPS), 16u | (32u << 8));
    TEST_EQ(bm_read8(r + GW100_DEV_CAPS + 3), 8);
}

/** SCRATCH 能写入并读回几种典型的值 */
static void
check_scratch(void)
{
    static const uint32_t pats[] = {
        0, 0xFFFFFFFFu, 0xA5A5A5A5u, 0x12345678u,
    };
    for (unsigned i = 0; i < sizeof(pats) / sizeof(pats[0]); i++) {
        bm_write32(r + GW100_SCRATCH, pats[i]);
        TEST_EQ(bm_read32(r + GW100_SCRATCH), pats[i]);
    }
}

/** 时间戳的频率、单调递增，以及读 LO 锁存 HI */
static void
check_timestamp(void)
{
    TEST_EQ(bm_read32(r + GW100_TIMESTAMP_FREQ), 100000000u);

    /* 64 位读一次取出 LO、HI；时间戳单调递增 */
    uint64_t t1 = bm_read64(r + GW100_TIMESTAMP_LO);
    for (volatile int i = 0; i < 100; i++)
        ;
    uint64_t t2 = bm_read64(r + GW100_TIMESTAMP_LO);
    TEST_ASSERT(t2 > t1);

    /* 两次 32 位读：读 LO 锁存 HI */
    uint32_t lo = bm_read32(r + GW100_TIMESTAMP_LO);
    uint32_t hi = bm_read32(r + GW100_TIMESTAMP_HI);
    uint64_t t3 = ((uint64_t)hi << 32) | lo;
    TEST_ASSERT(t3 > t2);
    TEST_INFO("时间戳 %llu → %llu → %llu\n", (unsigned long long)t1,
              (unsigned long long)t2, (unsigned long long)t3);
}

/**
 * 访问规则：保留偏移、非法宽度、第一次错误锁存、ERROR 位重新置位、
 * 未实现的块
 */
static void
check_access_rules(void)
{
    clear_errors();

    /* 保留偏移：读回 0，置 RSVD_ACCESS，记录第一次错误的位置 */
    TEST_EQ(bm_read32(r + 0x040), 0);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), GW100_ERR_RSVD_ACCESS);
    TEST_EQ(bm_read32(r + GW100_ERR_ADDR), 0x040);
    uint32_t info = bm_read32(r + GW100_ERR_INFO);
    TEST_EQ(info & GW100_ERR_INFO_WRITE, 0);
    TEST_EQ(GW100_ERR_INFO_SIZE(info), 4);
    TEST_EQ(GW100_ERR_INFO_BAR(info), 0);
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ERROR);

    /* 非法宽度的写被忽略；ERR_ADDR 仍是第一次错误的位置 */
    bm_write32(r + GW100_SCRATCH, 0x11223344u);
    bm_write16(r + GW100_SCRATCH, 0xFFFF);
    TEST_EQ(bm_read32(r + GW100_SCRATCH), 0x11223344u);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS),
            GW100_ERR_RSVD_ACCESS | GW100_ERR_BAD_WIDTH);
    TEST_EQ(bm_read32(r + GW100_ERR_ADDR), 0x040);

    /* 错误仍在时清 INT_STATUS.ERROR，会立即重新置位 */
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ERROR);
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ERROR);

    /* ERR_STATUS 写 1 清零后，下一次错误重新记录位置 */
    clear_errors();
    bm_write32(r + GW100_RSVD_BLOCK_HDI + 0x10, 1);    /* 尚未实现的块 */
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), GW100_ERR_RSVD_ACCESS);
    TEST_EQ(bm_read32(r + GW100_ERR_ADDR), GW100_RSVD_BLOCK_HDI + 0x10);
    TEST_ASSERT(bm_read32(r + GW100_ERR_INFO) & GW100_ERR_INFO_WRITE);
    clear_errors();
}

/** INT_MASK、INT_SET、INT_STATUS 的语义，以及跨两个寄存器的 64 位写 */
static void
check_interrupt_regs(void)
{
    clear_errors();
    TEST_EQ(bm_read32(r + GW100_INT_MASK), 0xFFFFFFFFu);   /* 复位值 */
    bm_write32(r + GW100_INT_MASK, 0x0000FFFFu);
    TEST_EQ(bm_read32(r + GW100_INT_MASK), 0x0000FFFFu);

    /* INT_SET 置位、读回 0；INT_STATUS 写 1 清零 */
    bm_write32(r + GW100_INT_SET, GW100_INT_ADMIN | (1u << 16));
    TEST_EQ(bm_read32(r + GW100_INT_SET), 0);
    TEST_EQ(bm_read32(r + GW100_INT_STATUS), GW100_INT_ADMIN | (1u << 16));
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ADMIN);
    TEST_EQ(bm_read32(r + GW100_INT_STATUS), 1u << 16);

    /* 64 位写同时更新 INT_STATUS（写 1 清零）和 INT_MASK */
    uint64_t mask_and_status = ((uint64_t)0xFFFFFFFFu << 32) | (1u << 16);
    bm_write64(r + GW100_INT_STATUS, mask_and_status);
    TEST_EQ(bm_read32(r + GW100_INT_STATUS), 0);
    TEST_EQ(bm_read32(r + GW100_INT_MASK), 0xFFFFFFFFu);
    clear_errors();
}

/**
 * 启动寄存器的桩值：固件已就绪、装载区上限；STAGE 不是 2 时 FW_SIZE
 * 的写入被忽略；0x4008、0x400C 是保留偏移
 */
static void
check_boot(void)
{
    uint32_t st = bm_read32(r + GW100_BOOT_STATUS);
    TEST_EQ(st & 0xF, 4);                       /* 固件运行 */
    TEST_ASSERT(st & GW100_BOOT_FW_READY);
    TEST_EQ(bm_read32(r + GW100_FW_LOAD_MAX), 1024u * 1024);
    TEST_EQ(bm_read32(r + GW100_BOOT_CTRL), 0);
    bm_write32(r + GW100_FW_SIZE, 0x1000);
    TEST_EQ(bm_read32(r + GW100_FW_SIZE), 0);
    TEST_EQ(bm_read32(r + GW100_FW_VERSION), 0);

    clear_errors();
    TEST_EQ(bm_read32(r + 0x4008), 0);
    TEST_EQ(bm_read32(r + GW100_ERR_STATUS), GW100_ERR_RSVD_ACCESS);
    TEST_EQ(bm_read32(r + GW100_ERR_ADDR), 0x4008);
    clear_errors();
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    r = g.bar0;
    TEST_INFO("BAR0 位于 %p\n", (void *)r);

    check_identity();
    check_scratch();
    check_timestamp();
    check_access_rules();
    check_interrupt_regs();
    check_boot();
    return 0;
}
