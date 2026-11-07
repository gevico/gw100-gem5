/**
 * @file  memory_defs.hh
 * @brief 通用内存共享定义；请勿手改
 * gw100-memory.json SHA-256=a7cf71d311d5780e9d9086c8a9762be83845bf73f4925fe848b0cbda69a5ad87
 * @see spec-dram
 */
#ifndef GW100_MEMORY_DEFS_HH
#define GW100_MEMORY_DEFS_HH
#include <cstdint>

namespace gem5::gw100::memorydefs {

inline constexpr uint64_t VERSION = 65536ULL;
inline constexpr uint64_t TILES = 4ULL;
inline constexpr uint64_t CHANNELS_PER_TILE = 2ULL;
inline constexpr uint64_t CHANNELS = 8ULL;
inline constexpr uint64_t RANKS_PER_CHANNEL = 2ULL;
inline constexpr uint64_t DEVICE_BYTES = 2147483648ULL;
inline constexpr uint64_t TOTAL_BYTES = 34359738368ULL;
inline constexpr uint64_t INTERLEAVE_BYTES = 256ULL;
inline constexpr uint64_t BURST_BYTES = 64ULL;
inline constexpr uint64_t TILE_WINDOW_BASE = 68719476736ULL;
inline constexpr uint64_t TILE_WINDOW_STRIDE = 134217728ULL;
inline constexpr uint64_t REGISTER_OFFSET = 8388608ULL;
inline constexpr uint64_t REGISTER_BYTES = 12288ULL;
inline constexpr uint64_t CHANNEL_BASE = 4096ULL;
inline constexpr uint64_t CHANNEL_STRIDE = 4096ULL;
inline constexpr uint64_t PROTECTION_BASE = 256ULL;
inline constexpr uint64_t PROTECTION_STRIDE = 32ULL;
inline constexpr uint64_t PROTECTION_ENTRIES = 4ULL;
inline constexpr uint64_t PROFILE_ID = 1ULL;
inline constexpr uint64_t DATA_RATE_MT_S = 8433ULL;
inline constexpr uint64_t CK_PS = 949ULL;
inline constexpr uint64_t SOURCE_LIMIT = 16ULL;
inline constexpr uint64_t FRONT_SLOTS = 64ULL;
inline constexpr uint64_t READ_SLOTS = 64ULL;
inline constexpr uint64_t WRITE_SLOTS = 64ULL;
inline constexpr uint64_t FIFO_DEPTH = 8ULL;
inline constexpr uint64_t FIFO_BYTES = 32ULL;
inline constexpr uint64_t SYNC_STAGES = 2ULL;
inline constexpr uint64_t PIPELINE_CYCLES = 2ULL;
inline constexpr uint64_t CAPABILITIES_VALUE = 7ULL;
inline constexpr uint64_t CORE_PS = 1000ULL;
inline constexpr uint64_t PCIE_PS = 4000ULL;
inline constexpr uint64_t TRFC_PS = 280000ULL;
inline constexpr uint64_t TXS_PS = 287500ULL;
inline constexpr uint64_t TREFI_PS = 3900000ULL;
inline constexpr uint64_t CALIBRATION_PS = 10000000ULL;
inline constexpr uint64_t TRAIN_PS = 100000000ULL;
inline constexpr uint64_t TIMEOUT_PS = 1000000000ULL;
inline constexpr uint64_t REG_VERSION = 0ULL;
inline constexpr uint64_t REG_TOPOLOGY = 4ULL;
inline constexpr uint64_t REG_STATUS = 8ULL;
inline constexpr uint64_t REG_CAPABILITIES = 12ULL;
inline constexpr uint64_t REG_EPOCH = 16ULL;
inline constexpr uint64_t REG_ERROR = 24ULL;
inline constexpr uint64_t REG_ERROR_SOURCE = 28ULL;
inline constexpr uint64_t REG_ERROR_ADDR = 32ULL;
inline constexpr uint64_t REG_ERROR_TXN = 40ULL;
inline constexpr uint64_t REG_ERROR_INFO = 48ULL;
inline constexpr uint64_t REG_PROTECTION_LOCK = 56ULL;
inline constexpr uint64_t REG_PERF_FREEZE = 60ULL;
inline constexpr uint64_t REG_REQUESTS = 64ULL;
inline constexpr uint64_t REG_READ_BYTES = 72ULL;
inline constexpr uint64_t REG_WRITE_BYTES = 80ULL;
inline constexpr uint64_t REG_RETRIES = 88ULL;
inline constexpr uint64_t REG_CDC_TICKS = 96ULL;
inline constexpr uint64_t REG_QUEUE_TICKS = 104ULL;
inline constexpr uint64_t REG_TRANSFER_TICKS = 112ULL;
inline constexpr uint64_t CH_PROFILE = 0ULL;
inline constexpr uint64_t CH_COMMAND = 4ULL;
inline constexpr uint64_t CH_STATE = 8ULL;
inline constexpr uint64_t CH_TRAIN_MASK = 12ULL;
inline constexpr uint64_t CH_ERROR = 16ULL;
inline constexpr uint64_t CH_SEQUENCE = 24ULL;
inline constexpr uint64_t CH_CK_PS = 32ULL;
inline constexpr uint64_t CMD_CONFIGURE = 1ULL;
inline constexpr uint64_t CMD_CALIBRATE = 2ULL;
inline constexpr uint64_t CMD_TRAIN = 3ULL;
inline constexpr uint64_t CMD_ENABLE = 4ULL;
inline constexpr uint64_t CMD_QUIESCE = 5ULL;
inline constexpr uint64_t CMD_SELF_REFRESH = 6ULL;
inline constexpr uint64_t CMD_RESUME = 7ULL;
inline constexpr uint64_t STATE_RESET = 0ULL;
inline constexpr uint64_t STATE_CONFIGURED = 1ULL;
inline constexpr uint64_t STATE_CALIBRATING = 2ULL;
inline constexpr uint64_t STATE_CALIBRATED = 3ULL;
inline constexpr uint64_t STATE_TRAINING = 4ULL;
inline constexpr uint64_t STATE_TEST = 5ULL;
inline constexpr uint64_t STATE_READY = 6ULL;
inline constexpr uint64_t STATE_QUIESCING = 7ULL;
inline constexpr uint64_t STATE_QUIESCED = 8ULL;
inline constexpr uint64_t STATE_SELF_REFRESH = 9ULL;
inline constexpr uint64_t STATE_RESUMING = 10ULL;
inline constexpr uint64_t STATE_FAILED = 15ULL;
inline constexpr uint64_t ERR_NONE = 0ULL;
inline constexpr uint64_t ERR_NOT_READY = 1ULL;
inline constexpr uint64_t ERR_PERMISSION = 2ULL;
inline constexpr uint64_t ERR_RANGE = 3ULL;
inline constexpr uint64_t ERR_CONFIG = 4ULL;
inline constexpr uint64_t ERR_BUSY = 5ULL;
inline constexpr uint64_t ERR_TRAIN = 6ULL;
inline constexpr uint64_t ERR_TIMEOUT = 7ULL;
inline constexpr uint64_t ERR_RESET = 8ULL;
inline constexpr uint64_t ERR_PROTOCOL = 9ULL;
inline constexpr uint64_t ERR_MEDIA = 10ULL;
inline constexpr uint64_t SOURCE_MGMT = 0ULL;
inline constexpr uint64_t SOURCE_HDMA = 1ULL;
inline constexpr uint64_t SOURCE_BAR2 = 2ULL;

} // namespace gem5::gw100::memorydefs
#endif
