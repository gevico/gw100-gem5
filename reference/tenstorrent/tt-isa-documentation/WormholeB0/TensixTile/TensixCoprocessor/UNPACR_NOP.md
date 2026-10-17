# `UNPACR_NOP`

This instruction has five distinct modes:
* [`UNPACR_NOP` (Set `SrcA` or `SrcB` to zero, sequenced with UNPACR)](UNPACR_NOP_ZEROSRC.md)
* [`UNPACR_NOP` (Give `SrcA` or `SrcB` banks to Matrix Unit, sequenced with UNPACR)](UNPACR_NOP_SETDVALID.md)
* [`UNPACR_NOP` (MMIO register write sequenced with UNPACR)](UNPACR_NOP_SETREG.md)
* [`UNPACR_NOP` (MMIO register write to Overlay `STREAM_MSG_DATA_CLEAR_REG_INDEX`, sequenced with UNPACR)](UNPACR_NOP_OverlayClear.md)
* [`UNPACR_NOP` (Occupy Unpacker for one cycle)](UNPACR_NOP_Nop.md)
