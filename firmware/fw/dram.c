/**
 * @file  dram.c
 * @brief 通用 Memory Tile 固件驱动的实现
 *
 * @see spec-dram, spec-mgmt-tile, spec-clock-reset-power
 */
#include "dram.h"

#ifndef GW_FW_COMPAT_MEMORY

#include "generated/gw100_memory_defs.h"
#include "gw100_mgmt.h"

/** 阶段超时，以 100 MHz mtime 周期计 */
#define DRAM_TIMEOUT (GW_MEM_TIMEOUT_PS / UINT64_C(10000))
/** 固件自检的阶段编码 */
#define DRAM_SELFTEST 8u

/** 供调试器检查的失败或当前阶段快照 */
volatile struct gw_dram_failure gw_dram_failure;

/** 当前 mtime 周期 */
static uint64_t
now(void)
{ return gw_read64(GW_CLINT_MTIME); }

/** 编址指定 Tile 的本地控制窗口 */
static uintptr_t
tile_base(unsigned tile)
{
    return GW_NI_BASE + GW_MEM_TILE_WINDOW_BASE +
           tile * GW_MEM_TILE_WINDOW_STRIDE + GW_MEM_REGISTER_OFFSET;
}

/** 编址指定全局通道的寄存器 */
static uintptr_t
channel_base(unsigned ch)
{
    return tile_base(ch / GW_MEM_CHANNELS_PER_TILE) + GW_MEM_CHANNEL_BASE +
           (ch % GW_MEM_CHANNELS_PER_TILE) * GW_MEM_CHANNEL_STRIDE;
}

/** 保存当前阶段或失败位置 */
static void
location(unsigned ch, unsigned phase, unsigned rank, uint64_t address)
{
    gw_dram_failure.tile = ch / GW_MEM_CHANNELS_PER_TILE;
    gw_dram_failure.channel = ch % GW_MEM_CHANNELS_PER_TILE;
    gw_dram_failure.rank = rank;
    gw_dram_failure.phase = phase;
    gw_dram_failure.address = address;
    gw_dram_failure.reason = 0;
}

/** 等待一个通道命令完成，失败时保留快照 */
static int
wait_state(unsigned ch, unsigned wanted, void (*service)(void))
{
    uintptr_t base = channel_base(ch);
    uint64_t start = now();
    for (;;) {
        unsigned state = gw_read32(base + GW_MEM_CH_STATE);
        unsigned error = gw_read32(base + GW_MEM_CH_ERROR);
        if (error || state == GW_MEM_STATE_FAILED) {
            gw_dram_failure.rank = (error >> 8) & 1;
            gw_dram_failure.reason = error & 0xFF;
            return -1;
        }
        if (state == wanted) {
            return 0;
        }
        if (now() - start >= DRAM_TIMEOUT) {
            unsigned mask = gw_read32(base + GW_MEM_CH_TRAIN_MASK);
            gw_dram_failure.rank = (mask & 1) ? 1 : 0;
            gw_dram_failure.reason = GW_MEM_ERR_TIMEOUT;
            return -1;
        }
        service();
    }
}

/** 并行发起八通道命令，然后分别检查完成状态 */
static int
all_command(unsigned command, unsigned wanted, void (*service)(void))
{
    for (unsigned ch = 0; ch < GW_MEM_CHANNELS; ++ch) {
        gw_write32(channel_base(ch) + GW_MEM_CH_COMMAND, command);
    }
    for (unsigned ch = 0; ch < GW_MEM_CHANNELS; ++ch) {
        location(ch, command, 0, 0);
        if (wait_state(ch, wanted, service)) {
            return -1;
        }
    }
    return 0;
}

/** 把通道内局部地址插入全局通道位 */
static uint64_t
global_address(unsigned ch, uint64_t local)
{ return (local & 255) | ((uint64_t)ch << 8) | ((local >> 8) << 11); }

/** 通过正常 NI 访问验证一个 rank 的首末 cache line */
static int
selftest(unsigned ch, unsigned rank, void (*service)(void))
{
    uint64_t capacity = GW_MEM_DEVICE_BYTES * GW_MEM_RANKS_PER_CHANNEL;
    uint64_t local[2] = {
        (uint64_t)rank << 15,
        ((capacity - GW_MEM_BURST_BYTES) & ~(UINT64_C(1) << 15)) |
            ((uint64_t)rank << 15)};
    for (unsigned edge = 0; edge < 2; ++edge) {
        uint64_t address = global_address(ch, local[edge]);
        location(ch, DRAM_SELFTEST, rank, address);
        for (unsigned pattern = 0; pattern < 2; ++pattern) {
            uint64_t value = UINT64_C(0x5AA5A55AC33C3CC3) ^ address;
            if (pattern) {
                value = ~value;
            }
            for (unsigned word = 0; word < GW_MEM_BURST_BYTES / 8; ++word) {
                gw_write64(GW_NI_BASE + address + 8 * word, value ^ word);
            }
            __asm__ volatile("fence iorw, iorw" ::: "memory");
            for (unsigned word = 0; word < GW_MEM_BURST_BYTES / 8; ++word) {
                if (gw_read64(GW_NI_BASE + address + 8 * word) !=
                    (value ^ word)) {
                    gw_dram_failure.reason = GW_MEM_ERR_MEDIA;
                    return -1;
                }
            }
            service();
        }
    }
    return 0;
}

/** 配置、训练、自检并开放八通道，等待期间调用 service */
int
dram_init(void (*service)(void))
{
    location(0, GW_MEM_CMD_CONFIGURE, 0, GW_CRC_PLL_DDR_CFG);
    gw_write32(GW_CRC_MESH_RST, 0x1Eu);
    gw_write32(GW_CRC_PLL_DDR_CFG, (GW_MEM_PROFILE_ID << 1) | 1);
    uint64_t start = now();
    while (!(gw_read32(GW_CRC_PLL_STATUS) & GW_PLL_DDR_LOCK)) {
        if (now() - start >= DRAM_TIMEOUT) {
            gw_dram_failure.reason = GW_MEM_ERR_TIMEOUT;
            return -1;
        }
        service();
    }
    gw_write32(GW_CRC_MESH_RST, 0);
    for (unsigned tile = 0; tile < GW_MEM_TILES; ++tile) {
        uintptr_t base = tile_base(tile);
        if (gw_read32(base + GW_MEM_REG_VERSION) != GW_MEM_VERSION ||
            gw_read32(base + GW_MEM_REG_TOPOLOGY) !=
                (tile | (2u << 8) | (2u << 16)) ||
            (gw_read32(base + GW_MEM_REG_CAPABILITIES) & 7u) != 7u) {
            location(tile * 2, GW_MEM_CMD_CONFIGURE, 0, base);
            gw_dram_failure.reason = GW_MEM_ERR_CONFIG;
            return -1;
        }
    }
    for (unsigned ch = 0; ch < GW_MEM_CHANNELS; ++ch) {
        gw_write32(channel_base(ch) + GW_MEM_CH_PROFILE, GW_MEM_PROFILE_ID);
    }
    if (all_command(GW_MEM_CMD_CONFIGURE, GW_MEM_STATE_CONFIGURED, service) ||
        all_command(GW_MEM_CMD_CALIBRATE, GW_MEM_STATE_CALIBRATED, service) ||
        all_command(GW_MEM_CMD_TRAIN, GW_MEM_STATE_TEST, service)) {
        return -1;
    }
    for (unsigned ch = 0; ch < GW_MEM_CHANNELS; ++ch) {
        if (gw_read32(channel_base(ch) + GW_MEM_CH_TRAIN_MASK) != 3u) {
            location(ch, GW_MEM_CMD_TRAIN, 0, channel_base(ch));
            gw_dram_failure.reason = GW_MEM_ERR_TRAIN;
            return -1;
        }
        for (unsigned rank = 0; rank < GW_MEM_RANKS_PER_CHANNEL; ++rank) {
            if (selftest(ch, rank, service)) {
                return -1;
            }
        }
    }
    if (all_command(GW_MEM_CMD_QUIESCE, GW_MEM_STATE_QUIESCED, service)) {
        return -1;
    }
    for (unsigned tile = 0; tile < GW_MEM_TILES; ++tile) {
        uintptr_t base = tile_base(tile);
        for (unsigned i = 0; i < GW_MEM_PROTECTION_ENTRIES; ++i) {
            uintptr_t r =
                base + GW_MEM_PROTECTION_BASE + i * GW_MEM_PROTECTION_STRIDE;
            gw_write64(r, 0);
            gw_write64(r + 8, 0);
            gw_write32(r + 16, 0);
            if (gw_read64(r) || gw_read64(r + 8) || gw_read32(r + 16)) {
                location(tile * 2, GW_MEM_CMD_ENABLE, 0, r);
                gw_dram_failure.reason = GW_MEM_ERR_CONFIG;
                return -1;
            }
        }
        gw_write32(base + GW_MEM_REG_PROTECTION_LOCK, 1);
        if (gw_read32(base + GW_MEM_REG_PROTECTION_LOCK) != 1u) {
            location(tile * 2, GW_MEM_CMD_ENABLE, 0, base);
            gw_dram_failure.reason = GW_MEM_ERR_CONFIG;
            return -1;
        }
    }
    if (all_command(GW_MEM_CMD_RESUME, GW_MEM_STATE_TEST, service) ||
        all_command(GW_MEM_CMD_ENABLE, GW_MEM_STATE_READY, service)) {
        return -1;
    }
    gw_dram_failure.phase = gw_dram_failure.reason = 0;
    return 0;
}

/** 排空八通道并进入自刷新，等待期间调用 service */
int
dram_suspend(void (*service)(void))
{
    if (all_command(GW_MEM_CMD_QUIESCE, GW_MEM_STATE_QUIESCED, service)) {
        return -1;
    }
    return all_command(GW_MEM_CMD_SELF_REFRESH, GW_MEM_STATE_SELF_REFRESH,
                       service);
}

/** 退出自刷新并满足恢复等待，等待期间调用 service */
int
dram_resume(void (*service)(void))
{ return all_command(GW_MEM_CMD_RESUME, GW_MEM_STATE_READY, service); }

#endif
