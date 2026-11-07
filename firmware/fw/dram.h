/**
 * @file  dram.h
 * @brief 通用 Memory Tile 的固件初始化与电源接口
 *
 * @see spec-dram, spec-mgmt-tile
 */
#ifndef FW_DRAM_H
#define FW_DRAM_H

#include <stdint.h>

/** 初始化或恢复失败的固件快照，保存在管理 SRAM */
struct gw_dram_failure
{
    /** Tile 编号 */
    uint32_t tile;
    /** Tile 内通道编号 */
    uint32_t channel;
    /** rank 编号 */
    uint32_t rank;
    /** 当前命令或自检阶段 */
    uint32_t phase;
    /** 控制器错误或固件检查结果 */
    uint32_t reason;
    /** 当前操作地址 */
    uint64_t address;
};

/** 供调试器检查的失败或当前阶段快照 */
extern volatile struct gw_dram_failure gw_dram_failure;
/** 配置、训练、自检并开放八通道，等待期间调用 service */
int dram_init(void (*service)(void));
/** 排空八通道并进入自刷新，等待期间调用 service */
int dram_suspend(void (*service)(void));
/** 退出自刷新并满足恢复等待，等待期间调用 service */
int dram_resume(void (*service)(void));

#endif
