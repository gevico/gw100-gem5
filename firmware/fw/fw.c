/**
 * @file  fw.c
 * @brief 管理 Tile 固件：初始化后置位 FW_READY，处理 FLR
 *
 * 本阶段的最小固件：等待 PLL_TS 锁定、切换到工作时钟、使能并锁定
 * 看门狗，设置并锁定 HDMA 描述符区间的访问控制，初始化 HDMA 的通道
 * 0、1，写入固件版本，置 STAGE = 4 与 FW_READY（spec-mgmt-tile §8.5
 * 中尚未建模的步骤留待后续阶段），然后在主循环中等中断（irq.c）、
 * 喂狗，并按 §9 处理 FLR（确认之后重新初始化 HDMA）。
 *
 * 以 GW_FW_HDMA_TEST 编译时是 HDMA 的测试固件：初始化之后运行
 * hdma_test.c 中的传输，把结果写入 FW_VERSION_FW（0xD0A0_0000 | 步骤号，
 * 0 为通过），再置位 FW_READY。以 GW_FW_INT_TEST 编译时是中断请求的
 * 测试固件：运行 int_test.c，结果写入 FW_VERSION_FW（0x1A70_0000 | 步骤
 * 号）。
 *
 * 以 GW_FW_WDT_TEST 编译时是看门狗的测试固件：初始化中自检第一级
 * 超时、PLIC 源 7 和合法喂狗，置位 FW_READY 后不再喂狗，只每 10 us
 * 把递增的计数写入 FW_VERSION_FW 作为心跳，等待第二级超时停住 CPU。
 *
 * 以 GW_FW_DEBUG_WIN 编译时，初始化和每次 FLR 之后开启 BAR2 调试窗口，
 * 供 host 的测试使用；正式固件只在 host 经 SET_FEATURE 请求时开启
 * （spec-pcie §7.3）。
 *
 * @see spec-mgmt-tile, spec-pcie
 */

#include <stdint.h>

#include "gw100_flr.h"
#include "gw100_hdma.h"
#include "gw100_image.h"
#include "gw100_mgmt.h"
#include "irq.h"

/** 镜像头：由 tools/fwpack.py 填写，链接时只占位 */
static const struct gw_image_hdr image_header
    __attribute__((section(".image_header"), used)) = {0};

/** 读 mtime */
static uint64_t
mtime(void)
{
    return *(volatile uint64_t *)GW_CLINT_MTIME;
}

/** 固件初始化失败：写启动错误码和 STAGE = 0xF，然后停止（§8.5） */
static void
init_fail(void)
{
    gw_write32(GW_FW_BOOT_STATUS,
               GW_BOOT_STATUS(GW_STAGE_FAILED, 0, GW_ERR_INIT));
    gw_halt();
}

#ifndef GW_FW_WDT_TEST
/** 每个通道的描述符环条目数 */
#define RING_ENTRIES    16

/** 通道 0、1 的描述符环：一块连续区域，访问控制按它设置 */
static struct {
    /** 两个环 */
    struct gw_hdma_desc r[2][RING_ENTRIES];
} rings __attribute__((aligned(64)));

/** 通道 0、1 的固件一侧状态 */
static struct gw_hdma_chan chans[2];

#ifdef GW_FW_HDMA_TEST
int hdma_selftest(struct gw_hdma_chan *c);
#endif
#ifdef GW_FW_INT_TEST
int int_selftest(void);
#endif
#endif

/** 看门狗的超时周期：10 ms，以 mtime 计 */
#define WDT_LOAD        1000000UL
/** 看门狗测试固件的超时周期：2 ms */
#define WDT_TEST_LOAD   200000UL
/** 看门狗测试固件的心跳间隔：10 us */
#define HEARTBEAT       1000UL

/**
 * 等待 PLL_TS 锁定（spec-mgmt-tile §8.5 第 1 步）：mtime 由它驱动，
 * 超时只能以 mcycle 计；每次轮询先读 TS_LOCK
 */
static void
ts_wait(void)
{
    uint64_t start = gw_mcycle();
    while (!(gw_read32(GW_CRC_PLL_STATUS) & GW_PLL_TS_LOCK)) {
        if (gw_mcycle() - start > GW_TS_LOCK_CYCLES)
            init_fail();
    }
}

/**
 * 切换到工作时钟（spec-clock-reset-power §3.4）：使能 PLL_CORE，在
 * clock.pll_lock_timeout 内等待锁定，把 clk_core、clk_mgmt 切换到 PLL
 * 并读回确认
 */
static void
clock_init(void)
{
    gw_write32(GW_CRC_PLL_CORE_CFG, 1u);
    uint64_t start = mtime();
    while (!(gw_read32(GW_CRC_PLL_STATUS) & GW_PLL_CORE_LOCK)) {
        if (mtime() - start > GW_PLL_LOCK_TIMEOUT)
            init_fail();
    }
    gw_write32(GW_CRC_CLK_SEL, 3u);
    if (gw_read32(GW_CRC_CLK_SEL) != 3u)
        init_fail();
}

/** 先写非零的 WDT_LOAD，再在同一次写入中置 EN 和 LOCK（§7.4） */
static void
wdt_init(uint64_t load)
{
    gw_write64(GW_WDT_LOAD, load);
    gw_write32(GW_WDT_CTRL, GW_WDT_EN | GW_WDT_LOCK);
    if (gw_read32(GW_WDT_CTRL) != (GW_WDT_EN | GW_WDT_LOCK))
        init_fail();
}

/** 合法喂狗 */
static void
wdt_kick(void)
{
    gw_write32(GW_WDT_KICK, GW_WDT_KICK_MAGIC);
}

#ifndef GW_FW_WDT_TEST
/**
 * 设置并锁定 SRAM 的访问控制：HDMA 只能读写描述符环所在的区间
 * （spec-mgmt-tile §4.4、§8.5 第 6 步）
 */
static void
acl_init(void)
{
    uintptr_t base = (uintptr_t)&rings;
    gw_write64(GW_ACL_DESC_BASE, base);
    gw_write64(GW_ACL_DESC_LIMIT, base + sizeof(rings));
    gw_write32(GW_ACL_LOCK, 1u);
}

/** 初始化 HDMA：使能，初始化通道 0、1 的环（§8.5 第 7 步） */
static void
hdma_init(void)
{
    gw_write32(GW_HDMA_CTRL, 1u);
    for (unsigned c = 0; c < 2; c++)
        gw_hdma_chan_init(&chans[c], c, rings.r[c], RING_ENTRIES);
}
#endif

#ifdef GW_FW_WDT_TEST
/**
 * 看门狗自检：锁定后 WDT_LOAD 不可改；不喂狗时第一级超时置 STAGE1 并
 * 挂起 PLIC 源 7；合法喂狗清除 STAGE1。失败按初始化失败处理
 */
static void
wdt_selftest(void)
{
    gw_write64(GW_WDT_LOAD, 1);
    if (gw_read64(GW_WDT_LOAD) != WDT_TEST_LOAD)
        init_fail();
    uint64_t start = mtime();
    while (!(gw_read32(GW_WDT_STATUS) & GW_WDT_STAGE1)) {
        if (mtime() - start > 2 * WDT_TEST_LOAD)
            init_fail();
    }
    if (!(gw_read32(GW_PLIC_PENDING) & (1u << GW_IRQ_WDT)))
        init_fail();
    wdt_kick();
    if (gw_read32(GW_WDT_STATUS) != 0)
        init_fail();
    if (gw_read32(GW_PLIC_PENDING) & (1u << GW_IRQ_WDT))
        init_fail();
}
#endif

/** 置位 FW_READY：固件可以接受命令 */
static void
set_ready(void)
{
#ifdef GW_FW_DEBUG_WIN
    gw_write32(GW_DEV_CTRL_FW, 1u);
#endif
    gw_write32(GW_FW_BOOT_STATUS,
               GW_BOOT_STATUS(GW_STAGE_FW_RUN, 1, GW_ERR_NONE));
}

#ifndef GW_FW_WDT_TEST
/** 处理 FLR（spec-mgmt-tile §9）：清理后确认，再重新置位 FW_READY */
static void
handle_flr(void)
{
    for (;;) {
        if (!(gw_flr_take() & GW_FLR_IN_PROGRESS))
            return;  /* 已经确认过的 FLR 留下的事件 */
        /* 复位前的 HDMA 事件属于旧的请求（spec-mgmt-tile §9）；本阶段
         * 没有命令协议的状态需要清理 */
        gw_write32(GW_EVT_PENDING, GW_EVT_HDMA_NOTIFY | GW_EVT_HDMA_ERR);
        if (gw_flr_confirm() != 0)
            continue;
        hdma_init();  /* FLR 复位了 HDMA */
        set_ready();
        if (!(gw_read32(GW_EVT_PENDING) & GW_EVT_FLR))
            return;
    }
}
#endif

/** 固件的主流程；info 为启动 ROM 交来的启动信息 */
void
fw_main(uint64_t hartid, const struct gw_boot_info *info)
{
    (void)hartid;
    (void)info;
    const struct gw_image_hdr *h = (const struct gw_image_hdr *)GW_SRAM_BASE;
    ts_wait();
    clock_init();
#ifdef GW_FW_WDT_TEST
    wdt_init(WDT_TEST_LOAD);
    wdt_selftest();
    gw_write32(GW_FW_VERSION_FW, h->fw_version);
    set_ready();
    uint64_t next = mtime();
    for (uint32_t n = 1;; n++) {
        while (mtime() < next)
            ;
        next += HEARTBEAT;
        gw_write32(GW_FW_VERSION_FW, n);
    }
#else
    wdt_init(WDT_LOAD);
    acl_init();
    hdma_init();
#if defined(GW_FW_HDMA_TEST)
    (void)h;
    gw_write32(GW_FW_VERSION_FW,
               0xD0A00000u | (uint32_t)hdma_selftest(&chans[1]));
#elif defined(GW_FW_INT_TEST)
    (void)h;
    gw_write32(GW_FW_VERSION_FW, 0x1A700000u | (uint32_t)int_selftest());
#else
    gw_write32(GW_FW_VERSION_FW, h->fw_version);
#endif
    irq_init(WDT_LOAD / 4);
    set_ready();
    for (;;) {
        /* 主循环每次醒来都喂狗；定时器保证空闲时至少每 1/4 周期醒来 */
        wdt_kick();
        uint32_t ev, flags;
        irq_wait(&ev, &flags);
        (void)flags;
        if (ev & GW_EVT_FLR)
            handle_flr();  /* 它自己写 1 清零 EVT_PENDING.FLR */
        /* 其余事件本阶段没有处理程序：清零后解除屏蔽 */
        gw_write32(GW_EVT_PENDING, ev & ~GW_EVT_FLR);
        irq_unmask(ev);
    }
#endif
}
