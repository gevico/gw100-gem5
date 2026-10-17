# Core-local Instruction RAM

RISCV E has a 16 KiB core-local instruction RAM. Instructions can be fetched and executed from this RAM, which can mitigate the relatively small instruction cache that RISCV E has (only ½ KiB), and can also help to reduce the load on L1.

This RAM exists in the address space at `ERISC_IRAM_BASE` (`0xFFC00000`), but this memory region appears as unmapped to the RISCV Load/Store Unit: stores to the region are silently discarded, and loads from the region never return. Instead, the only thing able to read from this RAM is the RISCV E Frontend, and the only thing able to write to this RAM is a bespoke piece of copy logic which repurposes the L1 access ports normally used by the Ethernet TX subsystem. Hence, to populate the RAM, its desired content needs to be written somewhere in L1, and then the copy logic needs to be instructed to copy that data from L1 to the instruction RAM. Once the copy has completed, the data is no longer required in L1.

A major difficulty with populating the instruction RAM is that the contents of the RAM can be corrupted if the RISCV E Frontend is reading from the instruction RAM on the same cycle as the copy logic is writing to it: there is no hardware interlock preventing simultaneous access. One side of this is easy: the copy logic will only ever be writing to the instruction RAM when explicitly instructed to perform a copy from L1 to instruction RAM. The other side is harder, as there are two reasons why the RISCV E Frontend might be reading from the instruction RAM:
* RISCV E is currently executing instructions out of the instruction RAM (i.e. `0xFFC00000 <= pc <= 0xFFC0FFFF`).
* RISCV E's branch predictor has predicted (possibly incorrectly) that there will soon be a control flow instruction jumping into the instruction RAM.

With this in mind, there are a few different ways of safely populating the instruction RAM:
* Put RISCV E into reset before instructing the copy logic, then bring it out of reset after the copy has finished. The assistance of some other core is required in order to instruct the copy logic once RISCV E is in reset, and the assistance of some other core is also required to bring RISCV E out of reset. Note that the contents of RISCV E's GPRs will be cleared to zero during reset, and its branch predictor history will also be lost.
* Ensure that RISCV E has not executed any instructions out of IRAM since it last came out of reset (as branch predictor history is erased during reset).
* Ensure that RISCV E is executing instructions out of L1, then have RISCV E deliberately pollute its branch predictor history in a way that means branch prediction targets will not be IRAM until IRAM is next executed from, then instruct the copy logic, and only jump to IRAM once the logic has finished.

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

## Copy logic

The copy logic always copies 16 KiB, starting from an arbitrary 16-byte aligned address in L1, to the entire IRAM (starting at address `0xFFC00000`). It is not possible to perform a smaller copy or a partial copy.

As the copy logic repurposes the L1 access ports normally used by the Ethernet TX subsystem, Ethernet RX and TX need to be disabled while the copy logic is being invoked. Example code in tt-metal [shows how to do this](https://github.com/tenstorrent/tt-metal/blob/0f97cf79f00077d7d99c445cc47f0c6ddfdc57a3/tt_metal/hw/firmware/src/erisc.cc#L65-L72): writing particular values to `0xFFBA0000` and `0xFFBA0004` to disable them, then writing different values to re-enable them.

Once all the prerequisites have been met, the copy logic can be invoked by writing the source L1 address, shifted right by four bits, to `ETH_CTRL_REGS_START + ETH_CORE_IRAM_LOAD` (`0xFFB9_4098`). Once invoked, the logic will remain active until it has finished copying 16 KiB. Software can determine when the logic has finished by reading back from the same address: the low bit will be set if the logic is active, and clear if it has finished.
