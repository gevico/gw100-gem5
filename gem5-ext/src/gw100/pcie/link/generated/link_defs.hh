/**
 * @file  link_defs.hh
 * @brief PCIe 共享定义，请勿手改
 * gw100-link.json SHA-256=c29b2defc6b3efc431d314589de4824b7cfd63e778c23721743321dd40a4dfea
 * @see spec-pcie
 */
#ifndef GW100_PCIE_LINK_DEFS_HH
#define GW100_PCIE_LINK_DEFS_HH
#include <cstdint>

namespace gem5::gw100::pciedefs {

inline constexpr uint64_t DEFAULT_GENERATION = 4ULL;
inline constexpr uint64_t DEFAULT_WIDTH = 4ULL;
inline constexpr uint64_t CONTROLLER_PERIOD_PS = 4000ULL;
inline constexpr uint64_t CONTROLLER_DATAPATH_BYTES = 32ULL;
inline constexpr uint64_t REFERENCE_CLOCK_HZ = 100000000ULL;
inline constexpr uint64_t TICK_PS = 1ULL;
inline constexpr uint64_t ADAPTER_CYCLES = 2ULL;
inline constexpr uint64_t ONE_WAY_PROPAGATION_PS = 5000ULL;
inline constexpr uint64_t MAX_PAYLOAD_BYTES = 256ULL;
inline constexpr uint64_t MAX_READ_REQUEST_BYTES = 512ULL;
inline constexpr uint64_t MAX_OUTSTANDING_READS = 64ULL;
inline constexpr uint64_t COMPLETION_DATA_BYTES = 32768ULL;
inline constexpr uint64_t MIN_RCB_BYTES = 64ULL;
inline constexpr uint64_t MAX_RCB_BYTES = 128ULL;
inline constexpr uint64_t COMPLETION_HEADER_SLOTS = 576ULL;
inline constexpr uint64_t DATA_CREDIT_BYTES = 16ULL;
inline constexpr uint64_t RX_PH = 32ULL;
inline constexpr uint64_t RX_PD_BYTES = 2048ULL;
inline constexpr uint64_t RX_NPH = 16ULL;
inline constexpr uint64_t RX_NPD_BYTES = 256ULL;
inline constexpr uint64_t REPLAY_DATA_BYTES = 4096ULL;
inline constexpr uint64_t REPLAY_ALIGNMENT_BYTES = 16ULL;
inline constexpr uint64_t REPLAY_DESCRIPTOR_SLOTS = 128ULL;
inline constexpr uint64_t REPLAY_DESCRIPTOR_BYTES = 16ULL;
inline constexpr uint64_t TX_PH = 32ULL;
inline constexpr uint64_t TX_PD_BYTES = 2048ULL;
inline constexpr uint64_t TX_NPH = 16ULL;
inline constexpr uint64_t TX_NPD_BYTES = 256ULL;
inline constexpr uint64_t TX_CPLH = 16ULL;
inline constexpr uint64_t TX_CPLD_BYTES = 4096ULL;
inline constexpr uint64_t TX_DLLP_SLOTS = 16ULL;
inline constexpr uint64_t BACKEND_SLOTS = 64ULL;
inline constexpr uint64_t RC_POSTED_RESERVED_SLOTS = 16ULL;
inline constexpr uint64_t HOST_PACKET_SLOTS = 64ULL;
inline constexpr uint64_t CREDIT_RELEASE_CYCLES = 2ULL;
inline constexpr uint64_t ACK_BATCH_TLPS = 8ULL;
inline constexpr uint64_t ASPM_L1_SUPPORTED = 1ULL;
inline constexpr uint64_t ASPM_L1_DEFAULT_ENABLED = 0ULL;
inline constexpr uint64_t BENCH_WARMUP_PS = 20000000ULL;
inline constexpr uint64_t BENCH_WINDOW_PS = 100000000ULL;
inline constexpr uint64_t BAR0_TARGET_PS = 500000ULL;
inline constexpr uint64_t BAR2_TARGET_PS = 1000000ULL;
inline constexpr uint64_t GEN1_BIT_HALF_PS = 800ULL;
inline constexpr uint64_t GEN1_SYMBOL_PS = 4000ULL;
inline constexpr uint64_t GEN1_MPS128_X1_ACK_SYMBOLS = 237ULL;
inline constexpr uint64_t GEN1_MPS128_X2_ACK_SYMBOLS = 128ULL;
inline constexpr uint64_t GEN1_MPS128_X4_ACK_SYMBOLS = 73ULL;
inline constexpr uint64_t GEN1_MPS256_X1_ACK_SYMBOLS = 416ULL;
inline constexpr uint64_t GEN1_MPS256_X2_ACK_SYMBOLS = 217ULL;
inline constexpr uint64_t GEN1_MPS256_X4_ACK_SYMBOLS = 118ULL;
inline constexpr uint64_t GEN2_BIT_HALF_PS = 400ULL;
inline constexpr uint64_t GEN2_SYMBOL_PS = 2000ULL;
inline constexpr uint64_t GEN2_MPS128_X1_ACK_SYMBOLS = 288ULL;
inline constexpr uint64_t GEN2_MPS128_X2_ACK_SYMBOLS = 179ULL;
inline constexpr uint64_t GEN2_MPS128_X4_ACK_SYMBOLS = 124ULL;
inline constexpr uint64_t GEN2_MPS256_X1_ACK_SYMBOLS = 467ULL;
inline constexpr uint64_t GEN2_MPS256_X2_ACK_SYMBOLS = 268ULL;
inline constexpr uint64_t GEN2_MPS256_X4_ACK_SYMBOLS = 169ULL;
inline constexpr uint64_t GEN3_BIT_HALF_PS = 250ULL;
inline constexpr uint64_t GEN3_SYMBOL_PS = 1000ULL;
inline constexpr uint64_t GEN3_MPS128_X1_ACK_SYMBOLS = 333ULL;
inline constexpr uint64_t GEN3_MPS128_X2_ACK_SYMBOLS = 224ULL;
inline constexpr uint64_t GEN3_MPS128_X4_ACK_SYMBOLS = 169ULL;
inline constexpr uint64_t GEN3_MPS256_X1_ACK_SYMBOLS = 512ULL;
inline constexpr uint64_t GEN3_MPS256_X2_ACK_SYMBOLS = 313ULL;
inline constexpr uint64_t GEN3_MPS256_X4_ACK_SYMBOLS = 214ULL;
inline constexpr uint64_t GEN4_BIT_HALF_PS = 125ULL;
inline constexpr uint64_t GEN4_SYMBOL_PS = 500ULL;
inline constexpr uint64_t GEN4_MPS128_X1_ACK_SYMBOLS = 333ULL;
inline constexpr uint64_t GEN4_MPS128_X2_ACK_SYMBOLS = 224ULL;
inline constexpr uint64_t GEN4_MPS128_X4_ACK_SYMBOLS = 169ULL;
inline constexpr uint64_t GEN4_MPS256_X1_ACK_SYMBOLS = 512ULL;
inline constexpr uint64_t GEN4_MPS256_X2_ACK_SYMBOLS = 313ULL;
inline constexpr uint64_t GEN4_MPS256_X4_ACK_SYMBOLS = 214ULL;
inline constexpr uint64_t REPLAY_SYMBOLS = 24576ULL;
inline constexpr uint64_t REPLAY_EXTENDED_SYMBOLS = 81920ULL;
inline constexpr uint64_t FC_UPDATE_PS = 30000000ULL;
inline constexpr uint64_t FC_UPDATE_EXTENDED_PS = 120000000ULL;
inline constexpr uint64_t FC_INIT_REPEAT_PS = 34000000ULL;
inline constexpr uint64_t SKP_SYMBOLS_INTERVAL = 1280ULL;
inline constexpr uint64_t SKP_BLOCKS_INTERVAL = 370ULL;
inline constexpr uint64_t LCRC_POLYNOMIAL = 79764919ULL;
inline constexpr uint64_t DLLP_CRC_POLYNOMIAL = 4107ULL;
inline constexpr uint64_t L1_IDLE_MIN_PS = 20000ULL;
inline constexpr uint64_t L1_EXIT_GUARD_PS = 40000ULL;
inline constexpr uint64_t PHY_FEEDBACK_PS = 100000ULL;
inline constexpr uint64_t PHY_EQ_EVALUATION_PS = 1000000ULL;
inline constexpr uint64_t ASPM_IDLE_PS = 10000000ULL;
inline constexpr uint64_t LT_DETECT_QUIET_PS = 12000000000ULL;
inline constexpr uint64_t LT_DETECT_RETURN_TO_GEN1_MIN_PS = 1000000000ULL;
inline constexpr uint64_t LT_POLLING_ACTIVE_PS = 24000000000ULL;
inline constexpr uint64_t LT_POLLING_CONFIGURATION_PS = 48000000000ULL;
inline constexpr uint64_t LT_CONFIGURATION_PS = 2000000000ULL;
inline constexpr uint64_t LT_RECOVERY_LOCK_PS = 24000000000ULL;
inline constexpr uint64_t LT_RECOVERY_CONFIG_PS = 48000000000ULL;
inline constexpr uint64_t LT_EQUALIZATION_EP_PHASE01_PS = 12000000000ULL;
inline constexpr uint64_t LT_EQUALIZATION_OTHER_PHASE_PS = 24000000000ULL;
inline constexpr uint64_t LT_HOT_RESET_RECEIVER_HOLD_PS = 2000000000ULL;
inline constexpr uint64_t LT_POLLING_ACTIVE_MIN_TS1 = 1024ULL;
inline constexpr uint64_t LT_POLLING_RECEIVE_CONSECUTIVE = 8ULL;
inline constexpr uint64_t LT_POLLING_CONFIG_MIN_TS2_AFTER_FIRST_RX = 16ULL;
inline constexpr uint64_t LT_CONFIG_LANE_RECEIVE_TS1 = 2ULL;
inline constexpr uint64_t LT_CONFIG_COMPLETE_RECEIVE_TS2 = 8ULL;
inline constexpr uint64_t LT_CONFIG_COMPLETE_MIN_TS2_AFTER_FIRST_RX = 16ULL;
inline constexpr uint64_t LT_IDLE_RECEIVE_SYMBOLS = 8ULL;
inline constexpr uint64_t LT_IDLE_TRANSMIT_SYMBOLS_AFTER_FIRST_RX = 16ULL;
inline constexpr uint64_t LT_RECOVERY_RECEIVE_TS = 8ULL;
inline constexpr uint64_t LT_RECOVERY_SPEED_TS2_8B10B = 32ULL;
inline constexpr uint64_t LT_RECOVERY_SPEED_TS2_128B130B = 128ULL;
inline constexpr uint64_t LT_EQ_PHASE_RECEIVE_TS1 = 2ULL;
inline constexpr uint64_t LT_EQ_REQUEST_HOLD_MIN_PS = 1000000ULL;
inline constexpr uint64_t LT_SPEED_IDLE_SUCCESS_PS = 800000ULL;
inline constexpr uint64_t LT_SPEED_IDLE_FAILURE_PS = 6000000ULL;
inline constexpr uint64_t LT_SPEED_IDLE_MAX_EXTRA_PS = 1000000000ULL;
inline constexpr uint64_t L1_REJECT_RETRY_PS = 10000000ULL;
inline constexpr uint64_t RC_CRS_RETRY_PS = 100000000ULL;
inline constexpr uint64_t RC_CRS_RETRIES = 10000ULL;
inline constexpr uint64_t CPL_TIMEOUT_A_SHORT_PS = 100000000ULL;
inline constexpr uint64_t CPL_TIMEOUT_A_LONG_PS = 10000000000ULL;
inline constexpr uint64_t CPL_TIMEOUT_B_SHORT_PS = 55000000000ULL;
inline constexpr uint64_t CPL_TIMEOUT_B_LONG_PS = 210000000000ULL;
inline constexpr uint64_t RC_LINK_WAIT_PS = 100000000000ULL;
inline constexpr uint64_t FRAME_CRC_MASK0 = 1239ULL;
inline constexpr uint64_t FRAME_CRC_MASK1 = 1724ULL;
inline constexpr uint64_t FRAME_CRC_MASK2 = 862ULL;
inline constexpr uint64_t FRAME_CRC_MASK3 = 431ULL;
inline constexpr uint64_t FRAME_PARITY_MASK = 1893ULL;

} // namespace gem5::gw100::pciedefs
#endif
