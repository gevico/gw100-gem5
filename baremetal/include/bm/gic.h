/**
 * @file  gic.h
 * @brief GICv3 与 ITS 的最小驱动：host 在 EL3 接收 SPI 与 LPI 中断
 *
 * 只做裸机测试需要的部分。distributor 关闭安全扩展（GICD_CTLR.DS = 1），
 * 只有一个安全状态，Group 1 中断（包括全部 LPI）以 IRQ 投递；SCR_EL3.IRQ
 * 把 IRQ 送到 EL3，由 vectors.S 的 IRQ 入口调用 bm_irq_dispatch，后者读
 * ICC_IAR1_EL1 应答、调用注册的处理程序、写 ICC_EOIR1_EL1 结束。
 *
 * ITS 的设备表、集合表、命令队列、ITT 和 redistributor 的 LPI 配置表、
 * pending 表放在 platform.h 的 BM_GIC_MEM_BASE 起的区域。PCI 设备写
 * GITS_TRANSLATER 的数据是 EventID，ITS 从请求的 stream ID（PCIe 的
 * Requester ID）得到 DeviceID，按 MAPTI 建立的映射置位对应的 LPI。
 *
 * @see spec-pcie
 */

#ifndef BM_GIC_H
#define BM_GIC_H

#include <stdint.h>

#include "bm/platform.h"

/** 设备的 MSI/MSI-X 消息写到这里，数据为 EventID */
#define BM_GITS_TRANSLATER      (BM_GITS_BASE + 0x10040)
/** 最小的 LPI 号 */
#define BM_LPI_BASE             8192u
/** 本驱动支持的 LPI 号上限（不含）：LPI 配置表按 14 位 ID 分配 */
#define BM_LPI_END              16384u
/** 支持的 DeviceID 上限（不含）：设备表一页 4 KiB，每项 8 B */
#define BM_ITS_MAX_DEVICE_ID    512u
/** 每个设备支持的 EventID 上限（不含） */
#define BM_ITS_MAX_EVENTS       32u

/** 中断处理程序：intid 为 GIC 的中断号 */
typedef void (*bm_irq_handler_t)(uint32_t intid);

/**
 * 初始化 GICv3 并打开 IRQ：distributor 置 DS、ARE 并使能 Group 1，唤醒
 * 本核的 redistributor，设置 CPU 接口（SRE、PMR、Group 1 使能），
 * SCR_EL3.IRQ = 1，清除 PSTATE.I。handler 处理之后到达的每个中断
 */
void bm_gic_init(bm_irq_handler_t handler);

/**
 * 初始化 LPI 与 ITS：建立 LPI 配置表和 pending 表并使能本核
 * redistributor 的 LPI，设置 ITS 的设备表、集合表和命令队列，使能 ITS，
 * 再用 MAPC 把集合 0 映射到本核。须在 bm_gic_init 之后调用。成功返回 0
 */
int bm_its_init(void);

/**
 * 把设备 device_id 的事件 event_id 映射到 LPI lpi，并使能该 LPI：
 * 设备第一次映射时发 MAPD，然后发 MAPTI、INV、SYNC 并等待 ITS 执行完。
 * 成功返回 0；参数越界、ITS 报错或超时返回 -1
 */
int bm_its_map(uint32_t device_id, uint32_t event_id, uint32_t lpi);

/** vectors.S 的 IRQ 入口调用：应答并处理全部待处理的 Group 1 中断 */
void bm_irq_dispatch(void);

#endif /* BM_GIC_H */
