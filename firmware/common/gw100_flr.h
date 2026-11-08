/**
 * @file  gw100_flr.h
 * @brief FLR 的取走与确认（spec-mgmt-tile §9），启动 ROM 与固件共用
 *
 * @see spec-mgmt-tile, spec-pcie
 */

#ifndef GW100_FLR_H
#define GW100_FLR_H

#include <stdint.h>

/** FLR_STATUS.IN_PROGRESS */
#define GW_FLR_IN_PROGRESS  (1u << 0)
/** FLR_STATUS.DELIVERED */
#define GW_FLR_DELIVERED    (1u << 1)
/** FLR_STATUS.FAIL */
#define GW_FLR_FAIL         (1u << 2)

/**
 * 第 1 步：清零 EVT_PENDING.FLR，写 TAKE，返回读到的 FLR_STATUS。
 * FAIL 时停住，不返回
 */
uint32_t gw_flr_take(void);

/**
 * 第 3 步：确认循环。返回 0 表示确认成功；返回 1 表示又有 FLR，
 * 调用者应回到第 1 步。FAIL 时停住，不返回
 */
int gw_flr_confirm(void);

/** 停住管理 CPU，等待 host 复位设备 */
void gw_halt(void) __attribute__((noreturn));

#endif /* GW100_FLR_H */
