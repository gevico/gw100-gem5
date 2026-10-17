# `RISCV_DEBUG_REG_SOFT_RESET_0`

Whereas Tensix tiles have a [plethora of soft reset bits](../TensixTile/SoftReset.md), Ethernet tiles have just one meaningful bit:

|Bit index|Purpose|
|--:|---|
|≤10|No effect|
|11|[RISCV E soft reset](#riscv-soft-reset)|
|≥12|No effect|

## RISCV soft reset

RISCV E has a single reset bit associated with it, which low-level software can make use of. The constant `RISCV_SOFT_RESET_0_BRISC` is available as a name for the appropriate bit mask (i.e. the reset bit for RISCV E is the same position as Tensix tiles use for RISCV B).

**Upon entering soft reset:** Any in-flight RISCV instructions are likely to be aborted, though software should not rely on this (especially if soft reset is only asserted for a handful of cycles). All [debug `DR` registers](../TensixTile/BabyRISCV/DebugInterface.md) are set to zero.

**Whilst held in soft reset:** New RISCV instructions will not start. The RISCV frontend will not perform any instruction reads against L1 nor against [core-local instruction RAM](BabyRISCV/InstructionRAM.md). Writes to [debug `DR` registers](../TensixTile/BabyRISCV/DebugInterface.md) are discarded.

**Upon leaving soft reset:** All branch predictor history will be forgotten. The [instruction cache](BabyRISCV/InstructionCache.md) will be invalidated. All GPRs will be set to zero, and `pc` will be set to the value contained at `0xFFB9409C`.
