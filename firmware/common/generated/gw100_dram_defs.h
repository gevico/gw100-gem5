/**
 * @file  gw100_dram_defs.h
 * @brief DRAM 已核实定义；请勿手改
 * 源：spec-src/dram/h58g66ck8bx147.json SHA-256=3f2b4e5a5452c0a983ace3f2b555bf32ad473c74af82c58a58411293f56a0f07
 * @see spec-dram
 */
#ifndef GW100_DRAM_DEFS_H
#define GW100_DRAM_DEFS_H
#include <stdint.h>

#define GW100_DRAM_SPEC_G0_READY 0
#if defined(GW100_REQUIRE_DRAM_G0) && !GW100_DRAM_SPEC_G0_READY
#error "SK hynix G0 source/review gate has not passed"
#endif

/* gw100: 本次批准的四个 Memory Tile，每 Tile 一个封装; count */
#define GW100_DRAM_PACKAGES UINT64_C(4)
/* SK1 p.3,5; count */
#define GW100_DRAM_CHANNELS_PER_PACKAGE UINT64_C(2)
/* SK1 p.5; count */
#define GW100_DRAM_RANKS_PER_CHANNEL UINT64_C(2)
/* SK1 p.5; bit */
#define GW100_DRAM_RANK_DENSITY_BITS UINT64_C(17179869184)
/* SK1 p.3,5; bit */
#define GW100_DRAM_CHANNEL_WIDTH_BITS UINT64_C(16)
/* derived: 4 封装 × 2 通道 × 2 rank × 16 Gibit / 8; byte */
#define GW100_DRAM_TOTAL_CAPACITY_BYTES UINT64_C(34359738368)
/* gw100: spec-address-map §3.2; byte */
#define GW100_DRAM_INTERLEAVE_BYTES UINT64_C(256)
/* gw100: 已批准的低速启动工作点；须满足 SK1 初始化频率范围; Hz */
#define GW100_DRAM_BOOT_CK_HZ UINT64_C(25000000)
/* gw100: 选取同时满足 SK1 p.269、p.273、p.586、p.591 的时钟; Hz */
#define GW100_DRAM_TARGET_CK_HZ UINT64_C(1066500000)
/* SK1 p.29; normal; ratio */
#define GW100_DRAM_TARGET_WCK_CK_RATIO UINT64_C(4)
/* gw100: 已批准的 BG、BL32 默认工作模式; beat */
#define GW100_DRAM_TARGET_BURST_BEATS UINT64_C(32)
/* SK1 p.269; normal; CK */
#define GW100_DRAM_TARGET_RL_CK UINT64_C(23)
/* SK1 p.273; normal; CK */
#define GW100_DRAM_TARGET_WL_CK UINT64_C(12)
/* SK1 p.269; normal; CK */
#define GW100_DRAM_TARGET_NRBTP_CK UINT64_C(6)
/* SK1 p.269; normal; MR2[3:0] */
#define GW100_DRAM_TARGET_RL_CODE UINT64_C(13)
/* SK1 p.273; normal; MR1[7:4] */
#define GW100_DRAM_TARGET_WL_CODE UINT64_C(13)
/* SK1 p.55; count */
#define GW100_DRAM_FSP_COUNT UINT64_C(3)
/* gw100: 已批准的可控环境输入默认值; milli°C */
#define GW100_DRAM_DEFAULT_TEMPERATURE_MILLIC UINT64_C(25000)
/* gw100: 本次批准的控制器资源上限，计入返回反压占用; slot */
#define GW100_DRAM_READ_SLOTS_PER_CHANNEL UINT64_C(64)
/* gw100: 本次批准的控制器资源上限，包含写数据; slot */
#define GW100_DRAM_WRITE_SLOTS_PER_CHANNEL UINT64_C(64)
/* gw100: 本次批准的队列容量单位; byte */
#define GW100_DRAM_SLOT_BYTES UINT64_C(64)
/* gw100: 本次批准的初始化命令队列上限; slot */
#define GW100_DRAM_INIT_COMMAND_SLOTS UINT64_C(16)
/* gw100: 本次批准的初始化结果队列上限; slot */
#define GW100_DRAM_INIT_RESULT_SLOTS UINT64_C(16)
/* gw100: 本次批准的各跨时钟 FIFO 上限; slot */
#define GW100_DRAM_CDC_FIFO_SLOTS UINT64_C(8)
/* gw100: 本次批准的 FIFO 指针同步级数; cycle */
#define GW100_DRAM_CDC_SYNC_STAGES UINT64_C(2)
/* gw100: 本次批准的请求控制流水级数; cycle */
#define GW100_DRAM_REQUEST_PIPELINE_STAGES UINT64_C(2)
/* gw100: 本次批准的响应控制流水级数; cycle */
#define GW100_DRAM_RESPONSE_PIPELINE_STAGES UINT64_C(2)
/* gw100: 本次批准的 FR-FCFS 防饥饿上限; count */
#define GW100_DRAM_MAX_YOUNGER_BYPASSES UINT64_C(8)
/* gw100: 本次批准的读写批次上限; burst */
#define GW100_DRAM_MAX_BATCH_BURSTS UINT64_C(16)
/* SK1 p.532; normal */
#define GW100_DRAM_TRCD_WRITE_MIN_PS UINT64_C(8000)
/* SK1 p.532; normal */
#define GW100_DRAM_TRCD_WRITE_MIN_CK UINT64_C(2)
/* SK1 p.532; normal */
#define GW100_DRAM_TRCD_READ_MASKED_MIN_PS UINT64_C(18000)
/* SK1 p.532; normal */
#define GW100_DRAM_TRCD_READ_MASKED_MIN_CK UINT64_C(2)
/* SK1 p.532; normal */
#define GW100_DRAM_TRP_ALL_MIN_PS UINT64_C(21000)
/* SK1 p.532; normal */
#define GW100_DRAM_TRP_ALL_MIN_CK UINT64_C(2)
/* SK1 p.532; normal */
#define GW100_DRAM_TRP_BANK_MIN_PS UINT64_C(18000)
/* SK1 p.532; normal */
#define GW100_DRAM_TRP_BANK_MIN_CK UINT64_C(2)
/* SK1 p.532; normal */
#define GW100_DRAM_TRAS_MIN_MIN_PS UINT64_C(42000)
/* SK1 p.532; normal */
#define GW100_DRAM_TRAS_MIN_MIN_CK UINT64_C(3)
/* SK1 p.532; normal */
#define GW100_DRAM_TWR_MIN_PS UINT64_C(34000)
/* SK1 p.532; normal */
#define GW100_DRAM_TWR_MIN_CK UINT64_C(3)
/* SK1 p.532; normal */
#define GW100_DRAM_TRRD_MIN_PS UINT64_C(3750)
/* SK1 p.532; normal */
#define GW100_DRAM_TRRD_MIN_CK UINT64_C(2)
/* SK1 p.532; normal */
#define GW100_DRAM_TFAW_MIN_PS UINT64_C(15000)
/* SK1 p.532; normal */
#define GW100_DRAM_TFAW_MIN_CK UINT64_C(0)
/* SK1 p.532; normal */
#define GW100_DRAM_TRBTP_MIN_PS UINT64_C(7500)
/* SK1 p.532; normal */
#define GW100_DRAM_TRBTP_MIN_CK UINT64_C(2)
/* SK1 p.532; normal */
#define GW100_DRAM_TRBTP_SUBTRACT_CK UINT64_C(2)
/* SK1 p.532; normal */
#define GW100_DRAM_TWTR_SHORT_MIN_PS UINT64_C(6250)
/* SK1 p.532; normal */
#define GW100_DRAM_TWTR_SHORT_MIN_CK UINT64_C(4)
/* SK1 p.532; normal */
#define GW100_DRAM_TWTR_LONG_MIN_PS UINT64_C(12000)
/* SK1 p.532; normal */
#define GW100_DRAM_TWTR_LONG_MIN_CK UINT64_C(4)
/* SK1 p.532; normal */
#define GW100_DRAM_TPPD_MIN_PS UINT64_C(0)
/* SK1 p.532; normal */
#define GW100_DRAM_TPPD_MIN_CK UINT64_C(2)
/* SK1 p.295; refresh */
#define GW100_DRAM_TRFC_ALL_MIN_PS UINT64_C(280000)
/* SK1 p.295; refresh */
#define GW100_DRAM_TRFC_ALL_MIN_CK UINT64_C(0)
/* SK1 p.295; refresh */
#define GW100_DRAM_TRFC_BANK_MIN_PS UINT64_C(140000)
/* SK1 p.295; refresh */
#define GW100_DRAM_TRFC_BANK_MIN_CK UINT64_C(0)
/* SK1 p.295; refresh */
#define GW100_DRAM_TPBR2PBR_MIN_PS UINT64_C(90000)
/* SK1 p.295; refresh */
#define GW100_DRAM_TPBR2PBR_MIN_CK UINT64_C(0)
/* SK1 p.295; refresh */
#define GW100_DRAM_TPBR2ACT_MIN_PS UINT64_C(7500)
/* SK1 p.295; refresh */
#define GW100_DRAM_TPBR2ACT_MIN_CK UINT64_C(0)
/* SK1 p.34 */
#define GW100_DRAM_MR1_DEFAULT_MASK UINT64_C(240)
/* SK1 p.34 */
#define GW100_DRAM_MR1_DEFAULT_VALUE UINT64_C(0)
/* SK1 p.34 */
#define GW100_DRAM_MR2_DEFAULT_MASK UINT64_C(255)
/* SK1 p.34 */
#define GW100_DRAM_MR2_DEFAULT_VALUE UINT64_C(0)
/* SK1 p.34 */
#define GW100_DRAM_MR3_DEFAULT_MASK UINT64_C(224)
/* SK1 p.34 */
#define GW100_DRAM_MR3_DEFAULT_VALUE UINT64_C(0)
/* SK1 p.34 */
#define GW100_DRAM_MR11_DEFAULT_MASK UINT64_C(119)
/* SK1 p.34 */
#define GW100_DRAM_MR11_DEFAULT_VALUE UINT64_C(0)
/* SK1 p.34 */
#define GW100_DRAM_MR12_DEFAULT_MASK UINT64_C(127)
/* SK1 p.34 */
#define GW100_DRAM_MR12_DEFAULT_VALUE UINT64_C(80)
/* SK1 p.34 */
#define GW100_DRAM_MR13_DEFAULT_MASK UINT64_C(128)
/* SK1 p.34 */
#define GW100_DRAM_MR13_DEFAULT_VALUE UINT64_C(0)
/* SK1 p.34 */
#define GW100_DRAM_MR14_DEFAULT_MASK UINT64_C(127)
/* SK1 p.34 */
#define GW100_DRAM_MR14_DEFAULT_VALUE UINT64_C(80)
/* SK1 p.34 */
#define GW100_DRAM_MR15_DEFAULT_MASK UINT64_C(127)
/* SK1 p.34 */
#define GW100_DRAM_MR15_DEFAULT_VALUE UINT64_C(80)
/* SK1 p.34 */
#define GW100_DRAM_MR16_DEFAULT_MASK UINT64_C(15)
/* SK1 p.34 */
#define GW100_DRAM_MR16_DEFAULT_VALUE UINT64_C(0)
/* SK1 p.34 */
#define GW100_DRAM_MR18_DEFAULT_MASK UINT64_C(128)
/* SK1 p.34 */
#define GW100_DRAM_MR18_DEFAULT_VALUE UINT64_C(128)
/* SK1 p.34 */
#define GW100_DRAM_MR28_DEFAULT_MASK UINT64_C(32)
/* SK1 p.34 */
#define GW100_DRAM_MR28_DEFAULT_VALUE UINT64_C(0)
/* SK1 p.29 */
#define GW100_DRAM_CAP_LINK_ECC UINT64_C(1)
/* GW100 控制器设计；spec-dram §7 */
#define GW100_DRAM_MMIO_VERSION UINT64_C(65536)
/* GW100 控制器设计；spec-dram §7 */
#define GW100_DRAM_MMIO_TILE_REGISTER_OFFSET UINT64_C(8388608)
/* GW100 控制器设计；spec-dram §7 */
#define GW100_DRAM_MMIO_CHANNEL_BASE UINT64_C(4096)
/* GW100 控制器设计；spec-dram §7 */
#define GW100_DRAM_MMIO_CHANNEL_STRIDE UINT64_C(4096)
/* GW100 控制器设计；spec-dram §7 */
#define GW100_DRAM_MMIO_PROTECTION_BASE UINT64_C(256)
/* GW100 控制器设计；spec-dram §7 */
#define GW100_DRAM_MMIO_PROTECTION_STRIDE UINT64_C(32)
/* GW100 控制器设计；spec-dram §7 */
#define GW100_DRAM_MMIO_PROTECTION_ENTRIES UINT64_C(4)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_VERSION UINT64_C(0)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_TOPOLOGY UINT64_C(4)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_STATUS UINT64_C(8)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_DEVICE_CAPABILITIES UINT64_C(12)
/* GW100 控制器设计；RO 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_RESET_EPOCH UINT64_C(16)
/* GW100 控制器设计；W1C 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_ERROR_STATUS UINT64_C(24)
/* GW100 控制器设计；RO 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_ERROR_ADDR UINT64_C(32)
/* GW100 控制器设计；RO 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_ERROR_TXN UINT64_C(40)
/* GW100 控制器设计；RO 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_ERROR_INFO UINT64_C(48)
/* GW100 控制器设计；W1S 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_PROTECTION_LOCK UINT64_C(56)
/* GW100 控制器设计；RW 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_PERF_FREEZE UINT64_C(60)
/* GW100 控制器设计；RO 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_COMMON_PERF_SEQUENCE UINT64_C(64)
/* GW100 控制器设计；W1S 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CONTROL UINT64_C(0)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_STATE UINT64_C(4)
/* GW100 控制器设计；RW 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CLOCK_SELECT UINT64_C(8)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CLOCK_STATUS UINT64_C(12)
/* GW100 控制器设计；RO 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CK_HZ_NUM UINT64_C(16)
/* GW100 控制器设计；RO 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CK_HZ_DEN UINT64_C(24)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CMD_CREDITS UINT64_C(32)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_RESULT_COUNT UINT64_C(36)
/* GW100 控制器设计；RW 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CMD_TAG UINT64_C(40)
/* GW100 控制器设计；RW 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CMD_KIND UINT64_C(44)
/* GW100 控制器设计；RW 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CMD_ARG0 UINT64_C(48)
/* GW100 控制器设计；RW 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CMD_ARG1 UINT64_C(56)
/* GW100 控制器设计；RW 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CMD_ARG2 UINT64_C(64)
/* GW100 控制器设计；W1S 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CMD_PUSH UINT64_C(72)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_CMD_PUSH_STATUS UINT64_C(76)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_RESULT_TAG UINT64_C(80)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_RESULT_STATUS UINT64_C(84)
/* GW100 控制器设计；RO 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_RESULT_VALUE UINT64_C(88)
/* GW100 控制器设计；RO 64 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_RESULT_TIME_PS UINT64_C(96)
/* GW100 控制器设计；W1S 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_RESULT_POP UINT64_C(104)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_TRAIN_PHASE UINT64_C(108)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_ERROR_STAGE UINT64_C(112)
/* GW100 控制器设计；RW 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_PHY_SELECT UINT64_C(116)
/* GW100 控制器设计；RW 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_PHY_SETTING UINT64_C(120)
/* GW100 控制器设计；RO 32 bit；spec-dram §7 */
#define GW100_DRAM_REG_CHANNEL_PHY_RESULT UINT64_C(124)

#endif
