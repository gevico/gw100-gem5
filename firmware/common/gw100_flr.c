/**
 * @file  gw100_flr.c
 * @brief FLR 的取走与确认的实现
 *
 * @see spec-mgmt-tile, spec-pcie
 */

#include "gw100_flr.h"

#include "gw100_mgmt.h"

/** 停住管理 CPU，等待 host 复位设备 */
void
gw_halt(void)
{
    for (;;)
        __asm__ volatile("wfi");
}

/**
 * 第 1 步：清零 EVT_PENDING.FLR，写 TAKE，返回读到的 FLR_STATUS。
 * FAIL 时停住，不返回
 */
uint32_t
gw_flr_take(void)
{
    gw_write32(GW_EVT_PENDING, GW_EVT_FLR);
    gw_write32(GW_FLR_CTRL, 1u << 0);
    uint32_t st = gw_read32(GW_FLR_STATUS);
    if (st & GW_FLR_FAIL)
        gw_halt();
    return st;
}

/**
 * 第 3 步：确认循环。返回 0 表示确认成功；返回 1 表示又有 FLR，
 * 调用者应回到第 1 步。FAIL 时停住，不返回
 */
int
gw_flr_confirm(void)
{
    for (;;) {
        if (gw_read32(GW_EVT_PENDING) & GW_EVT_FLR)
            return 1;
        gw_write32(GW_FLR_CTRL, 1u << 1);
        uint32_t st = gw_read32(GW_FLR_STATUS);
        if (st & GW_FLR_FAIL)
            gw_halt();
        if (!(st & GW_FLR_IN_PROGRESS))
            return 0;
        if (st & GW_FLR_DELIVERED)
            return 1;
    }
}
