"""
@file  sk_hynix_defs.py
@brief DRAM 已核实定义；请勿手改
源：spec-src/dram/h58g66ck8bx147.json SHA-256=3f2b4e5a5452c0a983ace3f2b555bf32ad473c74af82c58a58411293f56a0f07
@see spec-dram
"""

SPEC_G0_READY = False
PROFILE_SHA256 = '3f2b4e5a5452c0a983ace3f2b555bf32ad473c74af82c58a58411293f56a0f07'

# gw100: 本次批准的四个 Memory Tile，每 Tile 一个封装; count
PACKAGES = 4
# SK1 p.3,5; count
CHANNELS_PER_PACKAGE = 2
# SK1 p.5; count
RANKS_PER_CHANNEL = 2
# SK1 p.5; bit
RANK_DENSITY_BITS = 17179869184
# SK1 p.3,5; bit
CHANNEL_WIDTH_BITS = 16
# derived: 4 封装 × 2 通道 × 2 rank × 16 Gibit / 8; byte
TOTAL_CAPACITY_BYTES = 34359738368
# gw100: spec-address-map §3.2; byte
INTERLEAVE_BYTES = 256
# gw100: 已批准的低速启动工作点；须满足 SK1 初始化频率范围; Hz
BOOT_CK_HZ = 25000000
# gw100: 选取同时满足 SK1 p.269、p.273、p.586、p.591 的时钟; Hz
TARGET_CK_HZ = 1066500000
# SK1 p.29; normal; ratio
TARGET_WCK_CK_RATIO = 4
# gw100: 已批准的 BG、BL32 默认工作模式; beat
TARGET_BURST_BEATS = 32
# SK1 p.269; normal; CK
TARGET_RL_CK = 23
# SK1 p.273; normal; CK
TARGET_WL_CK = 12
# SK1 p.269; normal; CK
TARGET_NRBTP_CK = 6
# SK1 p.269; normal; MR2[3:0]
TARGET_RL_CODE = 13
# SK1 p.273; normal; MR1[7:4]
TARGET_WL_CODE = 13
# SK1 p.55; count
FSP_COUNT = 3
# gw100: 已批准的可控环境输入默认值; milli°C
DEFAULT_TEMPERATURE_MILLIC = 25000
# gw100: 本次批准的控制器资源上限，计入返回反压占用; slot
READ_SLOTS_PER_CHANNEL = 64
# gw100: 本次批准的控制器资源上限，包含写数据; slot
WRITE_SLOTS_PER_CHANNEL = 64
# gw100: 本次批准的队列容量单位; byte
SLOT_BYTES = 64
# gw100: 本次批准的初始化命令队列上限; slot
INIT_COMMAND_SLOTS = 16
# gw100: 本次批准的初始化结果队列上限; slot
INIT_RESULT_SLOTS = 16
# gw100: 本次批准的各跨时钟 FIFO 上限; slot
CDC_FIFO_SLOTS = 8
# gw100: 本次批准的 FIFO 指针同步级数; cycle
CDC_SYNC_STAGES = 2
# gw100: 本次批准的请求控制流水级数; cycle
REQUEST_PIPELINE_STAGES = 2
# gw100: 本次批准的响应控制流水级数; cycle
RESPONSE_PIPELINE_STAGES = 2
# gw100: 本次批准的 FR-FCFS 防饥饿上限; count
MAX_YOUNGER_BYPASSES = 8
# gw100: 本次批准的读写批次上限; burst
MAX_BATCH_BURSTS = 16
# SK1 p.532; normal
TRCD_WRITE_MIN_PS = 8000
# SK1 p.532; normal
TRCD_WRITE_MIN_CK = 2
# SK1 p.532; normal
TRCD_READ_MASKED_MIN_PS = 18000
# SK1 p.532; normal
TRCD_READ_MASKED_MIN_CK = 2
# SK1 p.532; normal
TRP_ALL_MIN_PS = 21000
# SK1 p.532; normal
TRP_ALL_MIN_CK = 2
# SK1 p.532; normal
TRP_BANK_MIN_PS = 18000
# SK1 p.532; normal
TRP_BANK_MIN_CK = 2
# SK1 p.532; normal
TRAS_MIN_MIN_PS = 42000
# SK1 p.532; normal
TRAS_MIN_MIN_CK = 3
# SK1 p.532; normal
TWR_MIN_PS = 34000
# SK1 p.532; normal
TWR_MIN_CK = 3
# SK1 p.532; normal
TRRD_MIN_PS = 3750
# SK1 p.532; normal
TRRD_MIN_CK = 2
# SK1 p.532; normal
TFAW_MIN_PS = 15000
# SK1 p.532; normal
TFAW_MIN_CK = 0
# SK1 p.532; normal
TRBTP_MIN_PS = 7500
# SK1 p.532; normal
TRBTP_MIN_CK = 2
# SK1 p.532; normal
TRBTP_SUBTRACT_CK = 2
# SK1 p.532; normal
TWTR_SHORT_MIN_PS = 6250
# SK1 p.532; normal
TWTR_SHORT_MIN_CK = 4
# SK1 p.532; normal
TWTR_LONG_MIN_PS = 12000
# SK1 p.532; normal
TWTR_LONG_MIN_CK = 4
# SK1 p.532; normal
TPPD_MIN_PS = 0
# SK1 p.532; normal
TPPD_MIN_CK = 2
# SK1 p.295; refresh
TRFC_ALL_MIN_PS = 280000
# SK1 p.295; refresh
TRFC_ALL_MIN_CK = 0
# SK1 p.295; refresh
TRFC_BANK_MIN_PS = 140000
# SK1 p.295; refresh
TRFC_BANK_MIN_CK = 0
# SK1 p.295; refresh
TPBR2PBR_MIN_PS = 90000
# SK1 p.295; refresh
TPBR2PBR_MIN_CK = 0
# SK1 p.295; refresh
TPBR2ACT_MIN_PS = 7500
# SK1 p.295; refresh
TPBR2ACT_MIN_CK = 0
# SK1 p.34
MR1_DEFAULT_MASK = 240
# SK1 p.34
MR1_DEFAULT_VALUE = 0
# SK1 p.34
MR2_DEFAULT_MASK = 255
# SK1 p.34
MR2_DEFAULT_VALUE = 0
# SK1 p.34
MR3_DEFAULT_MASK = 224
# SK1 p.34
MR3_DEFAULT_VALUE = 0
# SK1 p.34
MR11_DEFAULT_MASK = 119
# SK1 p.34
MR11_DEFAULT_VALUE = 0
# SK1 p.34
MR12_DEFAULT_MASK = 127
# SK1 p.34
MR12_DEFAULT_VALUE = 80
# SK1 p.34
MR13_DEFAULT_MASK = 128
# SK1 p.34
MR13_DEFAULT_VALUE = 0
# SK1 p.34
MR14_DEFAULT_MASK = 127
# SK1 p.34
MR14_DEFAULT_VALUE = 80
# SK1 p.34
MR15_DEFAULT_MASK = 127
# SK1 p.34
MR15_DEFAULT_VALUE = 80
# SK1 p.34
MR16_DEFAULT_MASK = 15
# SK1 p.34
MR16_DEFAULT_VALUE = 0
# SK1 p.34
MR18_DEFAULT_MASK = 128
# SK1 p.34
MR18_DEFAULT_VALUE = 128
# SK1 p.34
MR28_DEFAULT_MASK = 32
# SK1 p.34
MR28_DEFAULT_VALUE = 0
# SK1 p.29
CAP_LINK_ECC = 1
# GW100 控制器设计；spec-dram §7
MMIO_VERSION = 65536
# GW100 控制器设计；spec-dram §7
MMIO_TILE_REGISTER_OFFSET = 8388608
# GW100 控制器设计；spec-dram §7
MMIO_CHANNEL_BASE = 4096
# GW100 控制器设计；spec-dram §7
MMIO_CHANNEL_STRIDE = 4096
# GW100 控制器设计；spec-dram §7
MMIO_PROTECTION_BASE = 256
# GW100 控制器设计；spec-dram §7
MMIO_PROTECTION_STRIDE = 32
# GW100 控制器设计；spec-dram §7
MMIO_PROTECTION_ENTRIES = 4
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_COMMON_VERSION = 0
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_COMMON_TOPOLOGY = 4
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_COMMON_STATUS = 8
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_COMMON_DEVICE_CAPABILITIES = 12
# GW100 控制器设计；RO 64 bit；spec-dram §7
REG_COMMON_RESET_EPOCH = 16
# GW100 控制器设计；W1C 32 bit；spec-dram §7
REG_COMMON_ERROR_STATUS = 24
# GW100 控制器设计；RO 64 bit；spec-dram §7
REG_COMMON_ERROR_ADDR = 32
# GW100 控制器设计；RO 64 bit；spec-dram §7
REG_COMMON_ERROR_TXN = 40
# GW100 控制器设计；RO 64 bit；spec-dram §7
REG_COMMON_ERROR_INFO = 48
# GW100 控制器设计；W1S 32 bit；spec-dram §7
REG_COMMON_PROTECTION_LOCK = 56
# GW100 控制器设计；RW 32 bit；spec-dram §7
REG_COMMON_PERF_FREEZE = 60
# GW100 控制器设计；RO 64 bit；spec-dram §7
REG_COMMON_PERF_SEQUENCE = 64
# GW100 控制器设计；W1S 32 bit；spec-dram §7
REG_CHANNEL_CONTROL = 0
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_STATE = 4
# GW100 控制器设计；RW 32 bit；spec-dram §7
REG_CHANNEL_CLOCK_SELECT = 8
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_CLOCK_STATUS = 12
# GW100 控制器设计；RO 64 bit；spec-dram §7
REG_CHANNEL_CK_HZ_NUM = 16
# GW100 控制器设计；RO 64 bit；spec-dram §7
REG_CHANNEL_CK_HZ_DEN = 24
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_CMD_CREDITS = 32
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_RESULT_COUNT = 36
# GW100 控制器设计；RW 32 bit；spec-dram §7
REG_CHANNEL_CMD_TAG = 40
# GW100 控制器设计；RW 32 bit；spec-dram §7
REG_CHANNEL_CMD_KIND = 44
# GW100 控制器设计；RW 64 bit；spec-dram §7
REG_CHANNEL_CMD_ARG0 = 48
# GW100 控制器设计；RW 64 bit；spec-dram §7
REG_CHANNEL_CMD_ARG1 = 56
# GW100 控制器设计；RW 64 bit；spec-dram §7
REG_CHANNEL_CMD_ARG2 = 64
# GW100 控制器设计；W1S 32 bit；spec-dram §7
REG_CHANNEL_CMD_PUSH = 72
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_CMD_PUSH_STATUS = 76
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_RESULT_TAG = 80
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_RESULT_STATUS = 84
# GW100 控制器设计；RO 64 bit；spec-dram §7
REG_CHANNEL_RESULT_VALUE = 88
# GW100 控制器设计；RO 64 bit；spec-dram §7
REG_CHANNEL_RESULT_TIME_PS = 96
# GW100 控制器设计；W1S 32 bit；spec-dram §7
REG_CHANNEL_RESULT_POP = 104
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_TRAIN_PHASE = 108
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_ERROR_STAGE = 112
# GW100 控制器设计；RW 32 bit；spec-dram §7
REG_CHANNEL_PHY_SELECT = 116
# GW100 控制器设计；RW 32 bit；spec-dram §7
REG_CHANNEL_PHY_SETTING = 120
# GW100 控制器设计；RO 32 bit；spec-dram §7
REG_CHANNEL_PHY_RESULT = 124
