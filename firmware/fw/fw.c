/**
 * @file  fw.c
 * @brief 管理 Tile 固件：初始化后置位 FW_READY，处理 HDI 命令与 FLR
 *
 * 入口发布 STAGE = 4、FW_READY = 0，建立工作时钟、看门狗和事件服务。
 * dram.c 通过正常 NI 配置、训练和自检八通道内存，随后建立 HDMA 与 HDI。
 * 初始化成功后发布 FW_READY，主循环处理中断、FLR 和 D3hot/D0，
 * 初始化与恢复等待期间持续喂狗并处理启动事件。
 *
 * 以 GW_FW_HDMA_TEST 编译时是 HDMA 的测试固件：初始化之后运行
 * hdma_test.c 中的传输，把结果写入 FW_VERSION_FW（0xD0A0_0000 | 步骤号，
 * 0 为通过），再置位 FW_READY。以 GW_FW_INT_TEST 编译时是中断请求的
 * 测试固件：运行 int_test.c，结果写入 FW_VERSION_FW（0x1A70_0000 | 步骤
 * 号）。这两种测试固件不处理 HDI。
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

#include "dram.h"
#include "gw100_flr.h"
#include "gw100_hdma.h"
#include "gw100_image.h"
#include "gw100_mgmt.h"
#include "hdi.h"
#include "irq.h"

/** 启动流程的 HDMA 与协议初始化已经完成 */
static unsigned boot_complete;
/** 内存目前在自刷新保持 */
static unsigned memory_suspended;
/** 电源转换正在调用可重入的启动事件服务 */
static unsigned memory_changing;

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
#ifndef GW_FW_COMPAT_MEMORY
    if (gw_dram_failure.phase) {
        gw_write32(GW_FW_VERSION_FW,
                   0xD2000000u | (gw_dram_failure.tile << 20) |
                       (gw_dram_failure.channel << 18) |
                       (gw_dram_failure.rank << 16) |
                       (gw_dram_failure.phase << 8) | gw_dram_failure.reason);
    }
#endif
    gw_write32(GW_FW_BOOT_STATUS,
               GW_BOOT_STATUS(GW_STAGE_FAILED, 0, GW_ERR_INIT));
    gw_halt();
}

#if !defined(GW_FW_HDMA_TEST) && !defined(GW_FW_INT_TEST)
/**
 * 本固件处理 HDI 命令；HDMA、中断请求的测试固件不处理，镜像小一些，
 * 启动 ROM 的校验也快一些
 */
#define FW_HDI
#endif

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
    if (!boot_complete || memory_suspended || memory_changing ||
        (gw_read32(GW_PCI_STATE) & 0xCu)) {
        return;
    }
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
        /* 复位前的 HDMA 事件属于旧的请求（spec-mgmt-tile §9）；HDI 的
         * 状态随 FLR 复位 */
        gw_write32(GW_EVT_PENDING, GW_EVT_HDMA_NOTIFY | GW_EVT_HDMA_ERR);
#ifdef FW_HDI
        if (boot_complete) {
            hdi_reset();
        }
#endif
        if (gw_flr_confirm() != 0)
            continue;
        if (boot_complete && !memory_suspended && !memory_changing &&
            !(gw_read32(GW_PCI_STATE) & 0xCu)) {
            hdma_init();
            set_ready();
        }
        if (!(gw_read32(GW_EVT_PENDING) & GW_EVT_FLR))
            return;
    }
}
#endif

/** 初始化和电源等待期间喂狗，服务 FLR，并保留业务事件 */
static void
boot_service(void)
{
    wdt_kick();
    uint32_t events = gw_read32(GW_EVT_PENDING);
    if (events & GW_EVT_FLR) {
#ifndef GW_FW_WDT_TEST
        handle_flr();
#else
        while (gw_flr_take() & GW_FLR_IN_PROGRESS) {
            if (!gw_flr_confirm())
                break;
            wdt_kick();
        }
#endif
    }
    uint32_t handled = events & GW_EVT_PM;
    if (handled) {
        gw_write32(GW_EVT_PENDING, handled);
        irq_unmask(handled);
    }
}

#ifndef GW_FW_COMPAT_MEMORY
/** 启动完成前保持 D3hot，D0 后满足内存恢复等待 */
static void
boot_power_wait(void)
{
    for (;;) {
        if (gw_read32(GW_PCI_STATE) & 0xCu) {
            if (!memory_suspended) {
                if (dram_suspend(boot_service)) {
                    init_fail();
                }
                memory_suspended = 1;
            }
            boot_service();
        } else if (memory_suspended) {
            if (dram_resume(boot_service)) {
                init_fail();
            }
            memory_suspended = 0;
        } else {
            return;
        }
    }
}

#ifndef GW_FW_WDT_TEST
/** 处理运行期 D3hot/D0，并在恢复内存后重建 HDMA 与命令协议 */
static void
handle_power(void)
{
    if (!(gw_read32(GW_PCI_STATE) & 0xCu) && !memory_suspended) {
        return;
    }
    memory_changing = 1;
    gw_write32(GW_FW_BOOT_STATUS,
               GW_BOOT_STATUS(GW_STAGE_FW_RUN, 0, GW_ERR_NONE));
#ifdef FW_HDI
    hdi_reset();
#endif
    boot_power_wait();
    hdma_init();
    memory_changing = 0;
    set_ready();
}
#endif
#endif

/** 固件的主流程；info 为启动 ROM 交来的启动信息 */
void
fw_main(uint64_t hartid, const struct gw_boot_info *info)
{
    (void)hartid;
    (void)info;
    const struct gw_image_hdr *h = (const struct gw_image_hdr *)GW_SRAM_BASE;
    gw_write32(GW_FW_BOOT_STATUS,
               GW_BOOT_STATUS(GW_STAGE_FW_RUN, 0, GW_ERR_NONE));
    ts_wait();
    clock_init();
#ifdef GW_FW_WDT_TEST
    wdt_init(WDT_TEST_LOAD);
#ifndef GW_FW_COMPAT_MEMORY
    if (dram_init(boot_service)) {
        init_fail();
    }
    boot_power_wait();
#else
    boot_service();
#endif
    wdt_selftest();
    gw_write32(GW_FW_VERSION_FW, h->fw_version);
    boot_complete = 1;
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
#ifndef GW_FW_COMPAT_MEMORY
    if (dram_init(boot_service)) {
        init_fail();
    }
    boot_power_wait();
#else
    boot_service();
#endif
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
#ifdef FW_HDI
    hdi_init(&chans[0], &chans[1], h->fw_version);
#endif
    irq_init(WDT_LOAD / 4);
    boot_complete = 1;
    set_ready();
    int more = 0;
    for (;;) {
        /* 主循环每次醒来都喂狗；定时器保证空闲时至少每 1/4 周期醒来。
         * HDI 还有可以立即推进的工作时不睡眠 */
        wdt_kick();
        uint32_t ev, flags;
        uint64_t db;
        irq_wait(&ev, &db, &flags, !more);
        (void)flags;
        if (ev & GW_EVT_FLR)
            handle_flr();  /* 它自己写 1 清零 EVT_PENDING.FLR */
#ifndef GW_FW_COMPAT_MEMORY
        if ((ev & GW_EVT_PM) || memory_suspended) {
            handle_power();
        }
#endif
#ifdef FW_HDI
        uint32_t own = hdi_event(ev);
        if (db)
            hdi_doorbells();
        more = hdi_poll();
#else
        uint32_t own = 0;
        gw_write32(GW_DB_PENDING0, (uint32_t)db);
        gw_write32(GW_DB_PENDING1, (uint32_t)(db >> 32));
#endif
        /* 其余事件没有处理程序：清零后解除屏蔽 */
        gw_write32(GW_EVT_PENDING, ev & ~(GW_EVT_FLR | own));
        irq_unmask(ev);
        irq_unmask_db(db);
    }
#endif
}
