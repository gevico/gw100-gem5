/**
 * @file  irq.h
 * @brief 固件的中断框架：PLIC、CLINT 定时器、事件的屏蔽与交付
 *
 * 中断处理只记录，工作在主循环中做：
 * - doorbell 信箱（PLIC 源 1）：与事件相同，屏蔽 DB_PENDING 中未屏蔽的
 *   位并记下，主循环清零 pending、读槽位、处理之后再解除屏蔽；
 * - 事件寄存器（PLIC 源 2）：处理程序把 EVT_PENDING 中未屏蔽的位加入
 *   EVT_MASK 并记下，主循环处理完、写 1 清零 EVT_PENDING 之后再解除
 *   屏蔽（它是电平源，不屏蔽会反复进中断）；
 * - ML 错误（源 3）：记下 ML_ERR 并写 1 清零；
 * - 看门狗第一级（源 7）：记下并关闭该源，不喂狗，让第二级发生
 *   （spec-mgmt-tile §7.4 的建议）；
 * - CLINT 定时器：按固定周期唤醒主循环（例如喂狗）。
 *
 * @see spec-mgmt-tile
 */

#ifndef FW_IRQ_H
#define FW_IRQ_H

#include <stdint.h>

/** 中断框架记下的标志：定时器到期 */
#define IRQ_TIMER       (1u << 0)
/** 中断框架记下的标志：ML 错误 */
#define IRQ_ML_ERR      (1u << 1)
/** 中断框架记下的标志：看门狗第一级超时 */
#define IRQ_WDT         (1u << 2)

/** 初始化：PLIC 源 1、2、3、7，定时器周期 period（mtime 周期），开中断 */
void irq_init(uint64_t period);

/**
 * 取走中断框架记下的事件位（EVT_PENDING 的位，已被屏蔽）、doorbell 槽位
 * （DB_PENDING 的位，已被屏蔽）和标志；都没有且 block 不为 0 时在 wfi
 * 中等待中断。返回时中断是打开的
 */
void irq_wait(uint32_t *events, uint64_t *doorbells, uint32_t *flags,
              int block);

/** 事件位 bits 已处理并写 1 清零：解除屏蔽 */
void irq_unmask(uint32_t bits);

/** doorbell 槽位 slots 已处理（pending 已写 1 清零）：解除屏蔽 */
void irq_unmask_db(uint64_t slots);

/** 定时器最迟在 mtime 到达 t 时唤醒主循环（t = 0 表示没有额外的期限） */
void irq_deadline(uint64_t t);

#endif /* FW_IRQ_H */
