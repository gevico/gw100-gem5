/**
 * @file  hdi.h
 * @brief 固件的 HDI 命令处理（CP）：控制器、队列、命令、完成与中断
 *
 * 主循环每次醒来依次调用 hdi_event、hdi_doorbells、hdi_poll；全部工作
 * 在主循环中串行完成，没有并发。
 *
 * @see spec-hdi
 */

#ifndef FW_HDI_H
#define FW_HDI_H

#include <stdint.h>

#include "gw100_hdma.h"

/**
 * 初始化：ch0 为队列与完成记录使用的通道 0，ch1 为数据通道（都已
 * 初始化）；fw_version 写入 IDENTIFY。控制器处于未就绪状态
 */
void hdi_init(struct gw_hdma_chan *ch0, struct gw_hdma_chan *ch1,
              uint32_t fw_version);

/**
 * FLR 之后：HIF 中 HDI 的状态和 HDMA 已随 FLR 复位，固件丢弃自己的
 * 全部 HDI 状态和信箱中旧的 doorbell
 */
void hdi_reset(void);

/**
 * 处理事件 ev 中属于 HDI 的位（HDI_WR、HDMA_NOTIFY、HDMA_ERR），返回
 * 由本模块按协议清零的位；主循环不得再清零它们
 */
uint32_t hdi_event(uint32_t ev);

/** 处理 doorbell 信箱：先清零 DB_PENDING，再读取并检查被写过的槽位 */
void hdi_doorbells(void);

/**
 * 推进命令：回收数据通道的完成、写出完成记录、检查等待与计时、开始
 * 新命令，并按计时设置唤醒期限。返回非 0 表示本次的工作量用完时还有
 * 可以立即推进的工作，主循环不应睡眠
 */
int hdi_poll(void);

#endif /* FW_HDI_H */
