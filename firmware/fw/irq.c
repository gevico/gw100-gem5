/**
 * @file  irq.c
 * @brief 固件的中断框架
 *
 * @see spec-mgmt-tile
 */

#include "irq.h"

#include "gw100_flr.h"
#include "gw100_mgmt.h"

/** PLIC 的起始 */
#define PLIC_BASE       0x2C000000UL
/** 源 s 的优先级 */
#define PLIC_PRIO(s)    (PLIC_BASE + 4 * (s))
/** 上下文 0（M 模式）的使能 */
#define PLIC_EN0        (PLIC_BASE + 0x2000)
/** 上下文 0 的阈值 */
#define PLIC_THRESH0    (PLIC_BASE + 0x200000)
/** 上下文 0 的 claim/complete */
#define PLIC_CLAIM0     (PLIC_BASE + 0x200004)
/** CLINT 的 mtimecmp（hart 0） */
#define CLINT_MTIMECMP  0x20004000UL
/** PLIC 源：事件寄存器 */
#define SRC_EVT         2
/** PLIC 源：ML 错误 */
#define SRC_ML          3
/** PLIC 源：看门狗第一级 */
#define SRC_WDT         7
/** mie 与 mip 的位：机器定时器 */
#define MIE_MTIE        (1u << 7)
/** mie 与 mip 的位：机器外部中断 */
#define MIE_MEIE        (1u << 11)
/** mstatus.MIE */
#define MSTATUS_MIE     (1u << 3)

/** 处理程序记下、尚未交给主循环的事件位 */
static volatile uint32_t pending_events;
/** 处理程序记下、尚未交给主循环的标志 */
static volatile uint32_t pending_flags;
/** 定时器周期 */
static uint64_t tick;

void fw_trap_entry(void);
void fw_trap(uint64_t mcause);

/** 读 mtime */
static uint64_t
mtime(void)
{
    return *(volatile uint64_t *)GW_CLINT_MTIME;
}

/** 下一次定时器中断设在 period 之后 */
static void
timer_rearm(void)
{
    *(volatile uint64_t *)CLINT_MTIMECMP = mtime() + tick;
}

/** 初始化：PLIC 源 2、3、7，定时器周期 period（mtime 周期），开中断 */
void
irq_init(uint64_t period)
{
    tick = period;
    gw_write32(PLIC_PRIO(SRC_EVT), 1);
    gw_write32(PLIC_PRIO(SRC_ML), 1);
    gw_write32(PLIC_PRIO(SRC_WDT), 1);
    gw_write32(PLIC_EN0, (1u << SRC_EVT) | (1u << SRC_ML) |
                         (1u << SRC_WDT));
    gw_write32(PLIC_THRESH0, 0);
    timer_rearm();
    __asm__ volatile("csrw mtvec, %0" ::"r"(fw_trap_entry));
    __asm__ volatile("csrs mie, %0" ::"r"(MIE_MTIE | MIE_MEIE));
    __asm__ volatile("csrs mstatus, %0" ::"r"(MSTATUS_MIE));
}

/** 外部中断：claim、按源处理、complete */
static void
external(void)
{
    uint32_t src = gw_read32(PLIC_CLAIM0);
    switch (src) {
      case SRC_EVT: {
        uint32_t p = gw_read32(GW_EVT_PENDING) & ~gw_read32(GW_EVT_MASK);
        gw_write32(GW_EVT_MASK, gw_read32(GW_EVT_MASK) | p);
        pending_events |= p;
        break;
      }
      case SRC_ML:
        gw_write32(GW_ML_ERR, gw_read32(GW_ML_ERR));
        pending_flags |= IRQ_ML_ERR;
        break;
      case SRC_WDT:
        gw_write32(PLIC_EN0, gw_read32(PLIC_EN0) & ~(1u << SRC_WDT));
        pending_flags |= IRQ_WDT;
        break;
      default:
        break;
    }
    if (src)
        gw_write32(PLIC_CLAIM0, src);
}

/** trap 处理：中断交给各处理程序；异常是固件的错误，停住 */
void
fw_trap(uint64_t mcause)
{
    if (!(mcause >> 63))
        gw_halt();
    switch (mcause & 0x3F) {
      case 7:
        timer_rearm();
        pending_flags |= IRQ_TIMER;
        break;
      case 11:
        external();
        break;
      default:
        break;
    }
}

/**
 * 取走中断框架记下的事件位（EVT_PENDING 的位，已被屏蔽）和标志；
 * 都没有时在 wfi 中等待中断。返回时中断是打开的
 */
void
irq_wait(uint32_t *events, uint32_t *flags)
{
    for (;;) {
        // 关中断后检查，再 wfi：检查之后到达的中断使 wfi 立即返回，
        // 开中断后进入处理程序，不会睡过头
        __asm__ volatile("csrc mstatus, %0" ::"r"(MSTATUS_MIE));
        uint32_t e = pending_events;
        uint32_t f = pending_flags;
        pending_events = 0;
        pending_flags = 0;
        if (e == 0 && f == 0)
            __asm__ volatile("wfi");
        __asm__ volatile("csrs mstatus, %0" ::"r"(MSTATUS_MIE));
        if (e || f) {
            *events = e;
            *flags = f;
            return;
        }
    }
}

/** 事件位 bits 已处理并写 1 清零：解除屏蔽 */
void
irq_unmask(uint32_t bits)
{
    gw_write32(GW_EVT_MASK, gw_read32(GW_EVT_MASK) & ~bits);
}
