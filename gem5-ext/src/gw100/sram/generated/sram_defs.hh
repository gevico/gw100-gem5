/**
 * @file  sram_defs.hh
 * @brief SRAM 自动生成的共享定义
 * gw100-sram.json SHA-256=09404eb8799f963a264019f0be8138fda006cdb9ae4a49b8a2d616f7a93ae039
 * @see spec-sram
 */
#pragma once
#include <cstdint>
namespace gem5::gw100::sramdefs {

inline constexpr uint64_t MAX_REQUEST_BYTES = 64ULL;
inline constexpr uint64_t ECC_DATA_BYTES = 8ULL;
inline constexpr uint64_t ECC_CHECK_BITS = 8ULL;
inline constexpr uint64_t MAX_BANK_WAIT_CYCLES = 16ULL;
inline constexpr uint64_t MANAGEMENT_SIZE = 2097152ULL;
inline constexpr uint64_t MANAGEMENT_BANKS = 8ULL;
inline constexpr uint64_t MANAGEMENT_WORD_BYTES = 8ULL;
inline constexpr uint64_t MANAGEMENT_INTERLEAVED = 0ULL;
inline constexpr uint64_t MANAGEMENT_SPLIT_PORTS = 0ULL;
inline constexpr uint64_t MANAGEMENT_SOURCES = 3ULL;
inline constexpr uint64_t MANAGEMENT_SLOTS = 8ULL;
inline constexpr uint64_t MANAGEMENT_READ_CYCLES = 1ULL;
inline constexpr uint64_t MANAGEMENT_WRITE_CYCLES = 1ULL;
inline constexpr uint64_t MANAGEMENT_INTERVAL_CYCLES = 1ULL;
inline constexpr uint64_t MANAGEMENT_MASK_BYTES = 8ULL;
inline constexpr uint64_t MANAGEMENT_ECC = 1ULL;
inline constexpr uint64_t MANAGEMENT_PERIOD_PS = 2000ULL;
inline constexpr uint64_t MANAGEMENT_BOOT_PERIOD_PS = 40000ULL;
inline constexpr uint64_t COMPUTE_1RW_SIZE = 2097152ULL;
inline constexpr uint64_t COMPUTE_1RW_BANKS = 16ULL;
inline constexpr uint64_t COMPUTE_1RW_WORD_BYTES = 16ULL;
inline constexpr uint64_t COMPUTE_1RW_INTERLEAVED = 1ULL;
inline constexpr uint64_t COMPUTE_1RW_SPLIT_PORTS = 0ULL;
inline constexpr uint64_t COMPUTE_1RW_SOURCES = 5ULL;
inline constexpr uint64_t COMPUTE_1RW_SLOTS = 8ULL;
inline constexpr uint64_t COMPUTE_1RW_READ_CYCLES = 1ULL;
inline constexpr uint64_t COMPUTE_1RW_WRITE_CYCLES = 1ULL;
inline constexpr uint64_t COMPUTE_1RW_INTERVAL_CYCLES = 1ULL;
inline constexpr uint64_t COMPUTE_1RW_MASK_BYTES = 8ULL;
inline constexpr uint64_t COMPUTE_1RW_ECC = 1ULL;
inline constexpr uint64_t COMPUTE_1RW_PERIOD_PS = 1000ULL;
inline constexpr uint64_t COMPUTE_1RW_BOOT_PERIOD_PS = 1000ULL;
inline constexpr uint64_t COMPUTE_1R1W_SIZE = 2097152ULL;
inline constexpr uint64_t COMPUTE_1R1W_BANKS = 16ULL;
inline constexpr uint64_t COMPUTE_1R1W_WORD_BYTES = 16ULL;
inline constexpr uint64_t COMPUTE_1R1W_INTERLEAVED = 1ULL;
inline constexpr uint64_t COMPUTE_1R1W_SPLIT_PORTS = 1ULL;
inline constexpr uint64_t COMPUTE_1R1W_SOURCES = 5ULL;
inline constexpr uint64_t COMPUTE_1R1W_SLOTS = 8ULL;
inline constexpr uint64_t COMPUTE_1R1W_READ_CYCLES = 1ULL;
inline constexpr uint64_t COMPUTE_1R1W_WRITE_CYCLES = 1ULL;
inline constexpr uint64_t COMPUTE_1R1W_INTERVAL_CYCLES = 1ULL;
inline constexpr uint64_t COMPUTE_1R1W_MASK_BYTES = 8ULL;
inline constexpr uint64_t COMPUTE_1R1W_ECC = 1ULL;
inline constexpr uint64_t COMPUTE_1R1W_PERIOD_PS = 1000ULL;
inline constexpr uint64_t COMPUTE_1R1W_BOOT_PERIOD_PS = 1000ULL;
}
