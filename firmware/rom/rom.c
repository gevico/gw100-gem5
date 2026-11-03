/**
 * @file  rom.c
 * @brief 管理 Tile 的启动 ROM：等待 host 写入镜像、校验、跳转
 *
 * 按 spec-mgmt-tile §8.1 推进 STAGE：等 ML 建立后写 STAGE = 1、2，打开
 * 启动窗口；等待 FW_START 或 FLR；校验镜像头和 SHA-256（§8.2），失败
 * 回到 STAGE 2，成功后按 §8.3 设置 PMP、清零运行区、写启动信息
 * （§8.4）并跳转。FLR 按 §9 的取走与确认流程处理。启动 ROM 不访问
 * NoC、设备内存和 HDMA，不使能看门狗和中断。
 *
 * @see spec-mgmt-tile, spec-pcie
 */

#include <stddef.h>
#include <stdint.h>

#include "gw100_flr.h"
#include "gw100_image.h"
#include "gw100_mgmt.h"
#include "sha256.h"

/** 启动 ROM 的版本 */
#define ROM_VERSION 0x00010000u

/** 启动信息，位于 ROM 保留区起始处 */
static struct gw_boot_info boot_info
    __attribute__((section(".bootinfo"), used));

/** 本次启动中失败的加载次数 */
static uint32_t load_attempts;

/** 写 FW_BOOT_STATUS（经 ML） */
static void
set_boot_status(enum gw_stage stage, enum gw_boot_err err)
{
    gw_write32(GW_FW_BOOT_STATUS, GW_BOOT_STATUS(stage, 0, err));
}

/** 读 EVT_PENDING */
static uint32_t
evt_pending(void)
{
    return gw_read32(GW_EVT_PENDING);
}

/** 清零 EVT_PENDING 中的 bits */
static void
evt_clear(uint32_t bits)
{
    gw_write32(GW_EVT_PENDING, bits);
}

/**
 * 处理 FLR（spec-mgmt-tile §9 的取走与确认）：放弃本次加载，写
 * STAGE = 2，确认后启动窗口打开
 */
static void
handle_flr(void)
{
    for (;;) {
        if (!(gw_flr_take() & GW_FLR_IN_PROGRESS))
            return;  /* 已经确认过的 FLR 留下的事件 */
        /* 清理：放弃本次加载 */
        evt_clear(GW_EVT_FW_START);
        gw_write32(GW_FW_SIZE_FW, 0);
        set_boot_status(GW_STAGE_WAIT_LOAD, GW_ERR_NONE);
        if (gw_flr_confirm() == 0)
            return;
    }
}

/** 逐字节比较 n 字节，相同返回 1 */
static int
same(const uint8_t *a, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

/** 校验装载区中长度为 size 的镜像（spec-mgmt-tile §8.2），返回错误码 */
static enum gw_boot_err
verify_image(uint32_t size)
{
    if (size == 0 || size > GW_FW_LOAD_MAX || size % 8)
        return GW_ERR_SIZE;
    if (size < GW_IMAGE_HDR_SIZE)
        return GW_ERR_VERIFY;
    const struct gw_image_hdr *h = (const struct gw_image_hdr *)GW_SRAM_BASE;
    if (h->magic != GW_IMAGE_MAGIC || h->hdr_version != GW_IMAGE_HDR_VER ||
        h->hdr_size != GW_IMAGE_HDR_SIZE || h->image_size != size)
        return GW_ERR_VERIFY;
    if (h->entry_offset % 4 || h->entry_offset < GW_IMAGE_HDR_SIZE ||
        h->entry_offset >= size)
        return GW_ERR_VERIFY;
    if (h->reserved[0] || h->reserved[1] || h->reserved[2])
        return GW_ERR_VERIFY;
    uint8_t d[32];
    sha256((const uint8_t *)GW_SRAM_BASE + GW_IMAGE_HDR_SIZE,
           size - GW_IMAGE_HDR_SIZE, d);
    return same(d, h->digest, 32) ? GW_ERR_NONE : GW_ERR_VERIFY;
}

/** 由 CRC_RESET_CAUSE 得出启动信息中的复位原因 */
static uint32_t
boot_reason(void)
{
    uint32_t c = gw_read32(GW_CRC_RESET_CAUSE);
    if (c & (1u << 4))
        return 2;  /* 设备完全复位 */
    if (c & ((1u << 2) | (1u << 3)))
        return 1;  /* 热复位、Link Down */
    return 0;      /* 冷复位 */
}

/** 按 spec-mgmt-tile §8.3 设置 PMP：ROM RX，装载区 RWX，运行区 RW */
static void
setup_pmp(void)
{
    /* NAPOT：pmpaddr = (base | (size / 2 - 1)) >> 2 */
    uint64_t rom = (GW_ROM_BASE | ((64UL << 10) / 2 - 1)) >> 2;
    uint64_t load = (GW_SRAM_BASE | (GW_FW_LOAD_MAX / 2 - 1)) >> 2;
    uint64_t run = (GW_RUN_BASE | ((1UL << 20) / 2 - 1)) >> 2;
    /* 各项 A = NAPOT（0x18）：ROM R|X，装载区 R|W|X，运行区 R|W */
    uint64_t cfg = 0x1DULL | (0x1FULL << 8) | (0x1BULL << 16);
    __asm__ volatile("csrw pmpaddr0, %0" ::"r"(rom));
    __asm__ volatile("csrw pmpaddr1, %0" ::"r"(load));
    __asm__ volatile("csrw pmpaddr2, %0" ::"r"(run));
    __asm__ volatile("csrw pmpcfg0, %0" ::"r"(cfg));
}

/** 清零运行区中 ROM 保留区以外的部分 */
static void
clear_run_area(void)
{
    for (uint64_t *p = (uint64_t *)GW_RUN_BASE;
         p < (uint64_t *)GW_ROM_RSV_BASE; p++)
        *p = 0;
}

/** 校验通过：按 spec-mgmt-tile §8.3 交接，跳转到固件入口 */
static void __attribute__((noreturn))
start_firmware(void)
{
    const struct gw_image_hdr *h = (const struct gw_image_hdr *)GW_SRAM_BASE;
    void (*entry)(uint64_t, struct gw_boot_info *) =
        (void (*)(uint64_t, struct gw_boot_info *))(GW_SRAM_BASE +
                                                     h->entry_offset);
    setup_pmp();
    clear_run_area();
    boot_info.rom_version = ROM_VERSION;
    boot_info.boot_reason = boot_reason();
    boot_info.load_attempts = load_attempts;
    boot_info.sram_size = GW_SRAM_SIZE;
    boot_info.load_max = GW_FW_LOAD_MAX;
    __asm__ volatile("fence.i" ::: "memory");
    entry(0, &boot_info);
    gw_halt();
}

/** 启动 ROM 的主流程（spec-mgmt-tile §8.1 第 2 – 8 步） */
void
rom_main(void)
{
    /* 第 2 步：等待 ML 建立，不设超时 */
    while (!(gw_read32(GW_ML_STATUS) & 1u))
        ;
    /* 第 3、4 步 */
    set_boot_status(GW_STAGE_ROM_INIT, GW_ERR_NONE);
    set_boot_status(GW_STAGE_WAIT_LOAD, GW_ERR_NONE);

    for (;;) {
        /* 第 5 步：等待 FW_START 或 FLR */
        uint32_t ev;
        while (!((ev = evt_pending()) & (GW_EVT_FW_START | GW_EVT_FLR)))
            ;
        if (ev & GW_EVT_FLR) {
            handle_flr();
            continue;
        }
        evt_clear(GW_EVT_FW_START);

        /* 第 6 步：检查本次加载 */
        enum gw_boot_err err;
        if (gw_read32(GW_BOOT_LOAD_STATUS) & 1u)
            err = GW_ERR_WIN_WRITE;
        else
            err = verify_image(gw_read32(GW_FW_SIZE_FW));

        if (evt_pending() & GW_EVT_FLR) {
            handle_flr();
            continue;
        }
        if (err != GW_ERR_NONE) {
            /* 第 7 步：写启动错误码，清零镜像大小，回到 STAGE 2 */
            load_attempts++;
            gw_write32(GW_FW_SIZE_FW, 0);
            set_boot_status(GW_STAGE_WAIT_LOAD, err);
            continue;
        }
        /* 第 8 步：跳转前最后检查一次 FLR */
        if (evt_pending() & GW_EVT_FLR) {
            handle_flr();
            continue;
        }
        start_firmware();
    }
}
