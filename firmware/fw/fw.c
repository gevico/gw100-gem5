/**
 * @file  fw.c
 * @brief 管理 Tile 固件：初始化后置位 FW_READY，处理 FLR
 *
 * 本阶段的最小固件：写入固件版本，置 STAGE = 4 与 FW_READY
 * （spec-mgmt-tile §8.5 中尚未建模的步骤留待后续阶段），然后在主循环中
 * 按 §9 处理 FLR。以 GW_FW_DEBUG_WIN 编译时，初始化和每次 FLR 之后
 * 开启 BAR2 调试窗口，供 host 的测试使用；正式固件只在 host 经
 * SET_FEATURE 请求时开启（spec-pcie §7.3）。
 *
 * @see spec-mgmt-tile, spec-pcie
 */

#include <stdint.h>

#include "gw100_flr.h"
#include "gw100_image.h"
#include "gw100_mgmt.h"

/** 镜像头：由 tools/fwpack.py 填写，链接时只占位 */
static const struct gw_image_hdr image_header
    __attribute__((section(".image_header"), used)) = {0};

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

/** 处理 FLR（spec-mgmt-tile §9）：清理后确认，再重新置位 FW_READY */
static void
handle_flr(void)
{
    for (;;) {
        if (!(gw_flr_take() & GW_FLR_IN_PROGRESS))
            return;  /* 已经确认过的 FLR 留下的事件 */
        /* 本阶段没有命令协议和 HDMA 的状态需要清理 */
        if (gw_flr_confirm() != 0)
            continue;
        set_ready();
        if (!(gw_read32(GW_EVT_PENDING) & GW_EVT_FLR))
            return;
    }
}

/** 固件的主流程；info 为启动 ROM 交来的启动信息 */
void
fw_main(uint64_t hartid, const struct gw_boot_info *info)
{
    (void)hartid;
    (void)info;
    const struct gw_image_hdr *h = (const struct gw_image_hdr *)GW_SRAM_BASE;
    gw_write32(GW_FW_VERSION_FW, h->fw_version);
    set_ready();
    for (;;) {
        if (gw_read32(GW_EVT_PENDING) & GW_EVT_FLR)
            handle_flr();
    }
}
