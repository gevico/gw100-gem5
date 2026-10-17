# Core-local Instruction RAM

RISCV NC has a 16 KiB core-local instruction RAM. Instructions can be fetched and executed from this RAM, which can mitigate the relatively small instruction cache that RISCV NC has (only ½ KiB), and can also help to reduce the load on L1.

This RAM exists in the address space at `MEM_NCRISC_IRAM_BASE` (`0xFFC00000`), but this memory region appears as unmapped to the RISCV Load/Store Unit: stores to the region are silently discarded, and loads from the region never return. Instead, the only thing able to read from this RAM is the RISCV NC Frontend, and the only thing able to write to this RAM is [the mover](../Mover.md). Hence, to populate the RAM, its desired content needs to be written somewhere in L1, and then the mover needs to be instructed to copy that data from L1 to the instruction RAM. Once the copy has completed, the data is no longer required in L1.

A major difficulty with populating the instruction RAM is that the contents of the RAM can be corrupted if the RISCV NC Frontend is reading from the instruction RAM on the same cycle as the mover is writing to it: there is no hardware interlock preventing simultaneous access. One side of this is easy: the mover will only ever be writing to the instruction RAM when explicitly instructed to perform a copy from L1 to instruction RAM. The other side is harder, as there are two reasons why the RISCV NC Frontend might be reading from the instruction RAM:
* RISCV NC is currently executing instructions out of the instruction RAM (i.e. `0xFFC00000 <= pc <= 0xFFC0FFFF`).
* RISCV NC's branch predictor has predicted (possibly incorrectly) that there will soon be a control flow instruction jumping into the instruction RAM.

With this in mind, there are a few different ways of safely populating the instruction RAM:
* Put RISCV NC into reset before instructing the mover, then bring it out of reset after the mover has finished. The assistance of some other core is required in order to instruct the mover once RISCV NC is in reset, and the assistance of some other core is also required to bring RISCV NC out of reset. Note that the contents of RISCV NC's GPRs will be cleared to zero during reset, and its branch predictor history will also be lost.
* Ensure that RISCV NC has not executed any instructions out of IRAM since it last came out of reset (as branch predictor history is erased during reset).
* Clock-gate RISCV NC before instructing the mover, then revert this once the mover has finished. The assistance of some other core is required in order to instruct the mover once RISCV NC is gated, and the assistance of some other core is also required to revert things.
* Disable RISCV NC's branch predictor and ensure that RISCV NC is executing instructions out of L1, then instruct the mover, and only enable the branch predictor again and jump to IRAM once the mover has finished. Unfortunately, the registers for enabling and disabling the branch predictor are not mapped into RISCV NC's address space, so the assistance of some other core is required for those steps.
* Ensure that RISCV NC is executing instructions out of L1, then have RISCV NC deliberately pollute its branch predictor history in a way that means branch prediction targets will not be IRAM until IRAM is next executed from, then instruct the mover, and only jump to IRAM once the mover has finished.

The last option in the above list sounds somewhat exotic, but executing the following instruction sequence (from L1) will perform the necessary pollution:
```
.rept 13
bne x0, x0, .
bne x0, x0, .
nop
nop
.endr
bne x0, x0, .
bne x0, x0, .
```
Note that `.rept` / `.endr` needs to be expanded by the assembler, resulting in a sequence of 54 instructions - this sequence cannot be converted into a loop which just executes the inner instructions multiple times. Each `nop` instruction can be replaced by any other non-branching 4-byte instruction.
