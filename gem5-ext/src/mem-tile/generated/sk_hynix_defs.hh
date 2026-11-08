/**
 * @file  sk_hynix_defs.hh
 * @brief DRAM 已核实定义；请勿手改
 * 源：spec-src/dram/h58g66ck8bx147.json SHA-256=3f2b4e5a5452c0a983ace3f2b555bf32ad473c74af82c58a58411293f56a0f07
 * @see spec-dram
 */
#ifndef GW100_SK_HYNIX_DEFS_HH
#define GW100_SK_HYNIX_DEFS_HH
#include <cstdint>

namespace gem5::gw100::dram {
inline constexpr bool SpecG0Ready = false;
#ifdef GW100_REQUIRE_DRAM_G0
static_assert(SpecG0Ready, "SK hynix G0 source/review gate has not passed");
#endif

// gw100: 本次批准的四个 Memory Tile，每 Tile 一个封装; count
inline constexpr std::uint64_t PACKAGES = 4ULL;
// SK1 p.3,5; count
inline constexpr std::uint64_t CHANNELS_PER_PACKAGE = 2ULL;
// SK1 p.5; count
inline constexpr std::uint64_t RANKS_PER_CHANNEL = 2ULL;
// SK1 p.5; bit
inline constexpr std::uint64_t RANK_DENSITY_BITS = 17179869184ULL;
// SK1 p.3,5; bit
inline constexpr std::uint64_t CHANNEL_WIDTH_BITS = 16ULL;
// derived: 4 封装 × 2 通道 × 2 rank × 16 Gibit / 8; byte
inline constexpr std::uint64_t TOTAL_CAPACITY_BYTES = 34359738368ULL;
// gw100: spec-address-map §3.2; byte
inline constexpr std::uint64_t INTERLEAVE_BYTES = 256ULL;
// gw100: 已批准的低速启动工作点；须满足 SK1 初始化频率范围; Hz
inline constexpr std::uint64_t BOOT_CK_HZ = 25000000ULL;
// gw100: 选取同时满足 SK1 p.269、p.273、p.586、p.591 的时钟; Hz
inline constexpr std::uint64_t TARGET_CK_HZ = 1066500000ULL;
// SK1 p.29; normal; ratio
inline constexpr std::uint64_t TARGET_WCK_CK_RATIO = 4ULL;
// gw100: 已批准的 BG、BL32 默认工作模式; beat
inline constexpr std::uint64_t TARGET_BURST_BEATS = 32ULL;
// SK1 p.269; normal; CK
inline constexpr std::uint64_t TARGET_RL_CK = 23ULL;
// SK1 p.273; normal; CK
inline constexpr std::uint64_t TARGET_WL_CK = 12ULL;
// SK1 p.269; normal; CK
inline constexpr std::uint64_t TARGET_NRBTP_CK = 6ULL;
// SK1 p.269; normal; MR2[3:0]
inline constexpr std::uint64_t TARGET_RL_CODE = 13ULL;
// SK1 p.273; normal; MR1[7:4]
inline constexpr std::uint64_t TARGET_WL_CODE = 13ULL;
// SK1 p.55; count
inline constexpr std::uint64_t FSP_COUNT = 3ULL;
// gw100: 已批准的可控环境输入默认值; milli°C
inline constexpr std::uint64_t DEFAULT_TEMPERATURE_MILLIC = 25000ULL;
// gw100: 本次批准的控制器资源上限，计入返回反压占用; slot
inline constexpr std::uint64_t READ_SLOTS_PER_CHANNEL = 64ULL;
// gw100: 本次批准的控制器资源上限，包含写数据; slot
inline constexpr std::uint64_t WRITE_SLOTS_PER_CHANNEL = 64ULL;
// gw100: 本次批准的队列容量单位; byte
inline constexpr std::uint64_t SLOT_BYTES = 64ULL;
// gw100: 本次批准的初始化命令队列上限; slot
inline constexpr std::uint64_t INIT_COMMAND_SLOTS = 16ULL;
// gw100: 本次批准的初始化结果队列上限; slot
inline constexpr std::uint64_t INIT_RESULT_SLOTS = 16ULL;
// gw100: 本次批准的各跨时钟 FIFO 上限; slot
inline constexpr std::uint64_t CDC_FIFO_SLOTS = 8ULL;
// gw100: 本次批准的 FIFO 指针同步级数; cycle
inline constexpr std::uint64_t CDC_SYNC_STAGES = 2ULL;
// gw100: 本次批准的请求控制流水级数; cycle
inline constexpr std::uint64_t REQUEST_PIPELINE_STAGES = 2ULL;
// gw100: 本次批准的响应控制流水级数; cycle
inline constexpr std::uint64_t RESPONSE_PIPELINE_STAGES = 2ULL;
// gw100: 本次批准的 FR-FCFS 防饥饿上限; count
inline constexpr std::uint64_t MAX_YOUNGER_BYPASSES = 8ULL;
// gw100: 本次批准的读写批次上限; burst
inline constexpr std::uint64_t MAX_BATCH_BURSTS = 16ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRCD_WRITE_MIN_PS = 8000ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRCD_WRITE_MIN_CK = 2ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRCD_READ_MASKED_MIN_PS = 18000ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRCD_READ_MASKED_MIN_CK = 2ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRP_ALL_MIN_PS = 21000ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRP_ALL_MIN_CK = 2ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRP_BANK_MIN_PS = 18000ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRP_BANK_MIN_CK = 2ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRAS_MIN_MIN_PS = 42000ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRAS_MIN_MIN_CK = 3ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TWR_MIN_PS = 34000ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TWR_MIN_CK = 3ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRRD_MIN_PS = 3750ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRRD_MIN_CK = 2ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TFAW_MIN_PS = 15000ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TFAW_MIN_CK = 0ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRBTP_MIN_PS = 7500ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRBTP_MIN_CK = 2ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TRBTP_SUBTRACT_CK = 2ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TWTR_SHORT_MIN_PS = 6250ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TWTR_SHORT_MIN_CK = 4ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TWTR_LONG_MIN_PS = 12000ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TWTR_LONG_MIN_CK = 4ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TPPD_MIN_PS = 0ULL;
// SK1 p.532; normal
inline constexpr std::uint64_t TPPD_MIN_CK = 2ULL;
// SK1 p.295; refresh
inline constexpr std::uint64_t TRFC_ALL_MIN_PS = 280000ULL;
// SK1 p.295; refresh
inline constexpr std::uint64_t TRFC_ALL_MIN_CK = 0ULL;
// SK1 p.295; refresh
inline constexpr std::uint64_t TRFC_BANK_MIN_PS = 140000ULL;
// SK1 p.295; refresh
inline constexpr std::uint64_t TRFC_BANK_MIN_CK = 0ULL;
// SK1 p.295; refresh
inline constexpr std::uint64_t TPBR2PBR_MIN_PS = 90000ULL;
// SK1 p.295; refresh
inline constexpr std::uint64_t TPBR2PBR_MIN_CK = 0ULL;
// SK1 p.295; refresh
inline constexpr std::uint64_t TPBR2ACT_MIN_PS = 7500ULL;
// SK1 p.295; refresh
inline constexpr std::uint64_t TPBR2ACT_MIN_CK = 0ULL;
// SK1 p.34
inline constexpr std::uint64_t MR1_DEFAULT_MASK = 240ULL;
// SK1 p.34
inline constexpr std::uint64_t MR1_DEFAULT_VALUE = 0ULL;
// SK1 p.34
inline constexpr std::uint64_t MR2_DEFAULT_MASK = 255ULL;
// SK1 p.34
inline constexpr std::uint64_t MR2_DEFAULT_VALUE = 0ULL;
// SK1 p.34
inline constexpr std::uint64_t MR3_DEFAULT_MASK = 224ULL;
// SK1 p.34
inline constexpr std::uint64_t MR3_DEFAULT_VALUE = 0ULL;
// SK1 p.34
inline constexpr std::uint64_t MR11_DEFAULT_MASK = 119ULL;
// SK1 p.34
inline constexpr std::uint64_t MR11_DEFAULT_VALUE = 0ULL;
// SK1 p.34
inline constexpr std::uint64_t MR12_DEFAULT_MASK = 127ULL;
// SK1 p.34
inline constexpr std::uint64_t MR12_DEFAULT_VALUE = 80ULL;
// SK1 p.34
inline constexpr std::uint64_t MR13_DEFAULT_MASK = 128ULL;
// SK1 p.34
inline constexpr std::uint64_t MR13_DEFAULT_VALUE = 0ULL;
// SK1 p.34
inline constexpr std::uint64_t MR14_DEFAULT_MASK = 127ULL;
// SK1 p.34
inline constexpr std::uint64_t MR14_DEFAULT_VALUE = 80ULL;
// SK1 p.34
inline constexpr std::uint64_t MR15_DEFAULT_MASK = 127ULL;
// SK1 p.34
inline constexpr std::uint64_t MR15_DEFAULT_VALUE = 80ULL;
// SK1 p.34
inline constexpr std::uint64_t MR16_DEFAULT_MASK = 15ULL;
// SK1 p.34
inline constexpr std::uint64_t MR16_DEFAULT_VALUE = 0ULL;
// SK1 p.34
inline constexpr std::uint64_t MR18_DEFAULT_MASK = 128ULL;
// SK1 p.34
inline constexpr std::uint64_t MR18_DEFAULT_VALUE = 128ULL;
// SK1 p.34
inline constexpr std::uint64_t MR28_DEFAULT_MASK = 32ULL;
// SK1 p.34
inline constexpr std::uint64_t MR28_DEFAULT_VALUE = 0ULL;
// SK1 p.29
inline constexpr std::uint64_t CAP_LINK_ECC = 1ULL;
// GW100 控制器设计；spec-dram §7
inline constexpr std::uint64_t MMIO_VERSION = 65536ULL;
// GW100 控制器设计；spec-dram §7
inline constexpr std::uint64_t MMIO_TILE_REGISTER_OFFSET = 8388608ULL;
// GW100 控制器设计；spec-dram §7
inline constexpr std::uint64_t MMIO_CHANNEL_BASE = 4096ULL;
// GW100 控制器设计；spec-dram §7
inline constexpr std::uint64_t MMIO_CHANNEL_STRIDE = 4096ULL;
// GW100 控制器设计；spec-dram §7
inline constexpr std::uint64_t MMIO_PROTECTION_BASE = 256ULL;
// GW100 控制器设计；spec-dram §7
inline constexpr std::uint64_t MMIO_PROTECTION_STRIDE = 32ULL;
// GW100 控制器设计；spec-dram §7
inline constexpr std::uint64_t MMIO_PROTECTION_ENTRIES = 4ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_VERSION = 0ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_TOPOLOGY = 4ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_STATUS = 8ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_DEVICE_CAPABILITIES = 12ULL;
// GW100 控制器设计；RO 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_RESET_EPOCH = 16ULL;
// GW100 控制器设计；W1C 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_ERROR_STATUS = 24ULL;
// GW100 控制器设计；RO 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_ERROR_ADDR = 32ULL;
// GW100 控制器设计；RO 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_ERROR_TXN = 40ULL;
// GW100 控制器设计；RO 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_ERROR_INFO = 48ULL;
// GW100 控制器设计；W1S 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_PROTECTION_LOCK = 56ULL;
// GW100 控制器设计；RW 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_PERF_FREEZE = 60ULL;
// GW100 控制器设计；RO 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_COMMON_PERF_SEQUENCE = 64ULL;
// GW100 控制器设计；W1S 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CONTROL = 0ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_STATE = 4ULL;
// GW100 控制器设计；RW 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CLOCK_SELECT = 8ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CLOCK_STATUS = 12ULL;
// GW100 控制器设计；RO 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CK_HZ_NUM = 16ULL;
// GW100 控制器设计；RO 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CK_HZ_DEN = 24ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CMD_CREDITS = 32ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_RESULT_COUNT = 36ULL;
// GW100 控制器设计；RW 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CMD_TAG = 40ULL;
// GW100 控制器设计；RW 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CMD_KIND = 44ULL;
// GW100 控制器设计；RW 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CMD_ARG0 = 48ULL;
// GW100 控制器设计；RW 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CMD_ARG1 = 56ULL;
// GW100 控制器设计；RW 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CMD_ARG2 = 64ULL;
// GW100 控制器设计；W1S 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CMD_PUSH = 72ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_CMD_PUSH_STATUS = 76ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_RESULT_TAG = 80ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_RESULT_STATUS = 84ULL;
// GW100 控制器设计；RO 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_RESULT_VALUE = 88ULL;
// GW100 控制器设计；RO 64 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_RESULT_TIME_PS = 96ULL;
// GW100 控制器设计；W1S 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_RESULT_POP = 104ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_TRAIN_PHASE = 108ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_ERROR_STAGE = 112ULL;
// GW100 控制器设计；RW 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_PHY_SELECT = 116ULL;
// GW100 控制器设计；RW 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_PHY_SETTING = 120ULL;
// GW100 控制器设计；RO 32 bit；spec-dram §7
inline constexpr std::uint64_t REG_CHANNEL_PHY_RESULT = 124ULL;

} // namespace gem5::gw100::dram

#endif
