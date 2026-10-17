# Tile control / debug / status registers

An assortment of interesting registers exist in the address space starting at address `RISCV_DEBUG_REGS_START_ADDR` (`0xFFB12000`), and some more start at address `ETH_CTRL_REGS_START` (`0xFFB94000`). These are accessible to RISCV E and to external clients via the NoC and via Ethernet. Amongst other things, this region of the address space includes:
* [Soft reset](SoftReset.md) (`RISCV_DEBUG_REG_SOFT_RESET_0`)
* [Cycle counters](CycleCounters.md) (`RISCV_DEBUG_REG_WALL_CLOCK_L`, `RISCV_DEBUG_REG_WALL_CLOCK_H`, `RISCV_DEBUG_REG_WDT`).
* [The RISCV GDB/Debug Interface](../TensixTile/BabyRISCV/DebugInterface.md#debug-register-access) (`RISCV_DEBUG_REG_RISC_DBG_CNTL_0`, `RISCV_DEBUG_REG_RISC_DBG_CNTL_1`, `RISCV_DEBUG_REG_RISC_DBG_STATUS_0`, `RISCV_DEBUG_REG_RISC_DBG_STATUS_1`)
* [The debug daisychain](../TensixTile/DebugDaisychain.md) (`RISCV_DEBUG_REG_DBG_BUS_CNTL_REG`, `RISCV_DEBUG_REG_DBG_RD_DATA`, `RISCV_DEBUG_REG_DBG_L1_MEM_REG2` et al.)

## Spare registers available for general use

Various registers within this address space region have a defined purpose in Tensix tiles, but have no defined purpose in Ethernet tiles. They are still present, and available for general use rather than having a specific defined purpose.

|Address|Conventional name|Ethernet tile usage|
|---|---|---|
|`0xFFB1_2000`|`RISCV_DEBUG_REG_PERF_CNT_INSTRN_THREAD0`|32 bits available for general use|
|`0xFFB1_2004`|`RISCV_DEBUG_REG_PERF_CNT_INSTRN_THREAD1`|32 bits available for general use|
|`0xFFB1_2008`|`RISCV_DEBUG_REG_PERF_CNT_INSTRN_THREAD2`|32 bits available for general use|
|`0xFFB1_200C`|`RISCV_DEBUG_REG_PERF_CNT_TDMA_UNPACK0`|32 bits available for general use|
|`0xFFB1_2010`|`RISCV_DEBUG_REG_PERF_CNT_TDMA_UNPACK1`|32 bits available for general use|
|`0xFFB1_2014`|`RISCV_DEBUG_REG_PERF_CNT_TDMA_UNPACK2`|32 bits available for general use|
|`0xFFB1_2018`|`RISCV_DEBUG_REG_PERF_CNT_FPU0`|32 bits available for general use|
|`0xFFB1_201C`|`RISCV_DEBUG_REG_PERF_CNT_FPU1`|32 bits available for general use|
|`0xFFB1_2020`|`RISCV_DEBUG_REG_PERF_CNT_FPU2`|32 bits available for general use|
|`0xFFB1_203C`|`RISCV_DEBUG_REG_PERF_CNT_ALL`|32 bits available for general use|
|`0xFFB1_2058`|`RISCV_DEBUG_REG_CFGREG_RD_CNTL`|32 bits available for general use|
|`0xFFB1_2064`|`RISCV_DEBUG_REG_DBG_ARRAY_RD_CMD`|32 bits available for general use|
|`0xFFB1_2070`|`RISCV_DEBUG_REG_CG_CTRL_HYST0`|32 bits available for general use|
|`0xFFB1_2074`|`RISCV_DEBUG_REG_CG_CTRL_HYST1`|32 bits available for general use|
|`0xFFB1_20A4`|`RISCV_DEBUG_REG_INSTRN_BUF_CTRL1`|32 bits available for general use|
|`0xFFB1_20AC`|`RISCV_DEBUG_REG_STOCH_RND_MASK0`|32 bits available for general use|
|`0xFFB1_20B0`|`RISCV_DEBUG_REG_STOCH_RND_MASK1`|32 bits available for general use|
|`0xFFB1_20F0`|`RISCV_DEBUG_REG_PERF_CNT_TDMA_PACK0`|32 bits available for general use|
|`0xFFB1_20F4`|`RISCV_DEBUG_REG_PERF_CNT_TDMA_PACK1`|32 bits available for general use|
|`0xFFB1_20F8`|`RISCV_DEBUG_REG_PERF_CNT_TDMA_PACK2`|32 bits available for general use|
|`0xFFB1_2200`|`RISCV_DEBUG_REG_TIMESTAMP_DUMP_CNTL`|32 bits available for general use|
|`0xFFB1_2208`|`RISCV_DEBUG_REG_TIMESTAMP_DUMP_BUF0_START_ADDR`|32 bits available for general use|
|`0xFFB1_220C`|`RISCV_DEBUG_REG_TIMESTAMP_DUMP_BUF0_END_ADDR`|32 bits available for general use|
|`0xFFB1_2210`|`RISCV_DEBUG_REG_TIMESTAMP_DUMP_BUF1_START_ADDR`|32 bits available for general use|
|`0xFFB1_2214`|`RISCV_DEBUG_REG_TIMESTAMP_DUMP_BUF1_END_ADDR`|32 bits available for general use|
|`0xFFB9_4084`|`ETH_CTRL_REGS_START`::`ETH_CORE_SCRATCH0`|32 bits available for general use|
|`0xFFB9_4088`|`ETH_CTRL_REGS_START`::`ETH_CORE_SCRATCH1`|32 bits available for general use|
|`0xFFB9_408C`|`ETH_CTRL_REGS_START`::`ETH_CORE_SCRATCH2`|32 bits available for general use|
|`0xFFB9_4090`|`ETH_CTRL_REGS_START`::`ETH_CORE_SCRATCH3`|32 bits available for general use|
