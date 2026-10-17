# Instruction Set

The "baby" RISCV cores implement 32-bit RISCV with some extensions:
* RV32I Base Integer Instruction Set, Version 2.1, with two minor caveats:
  * `ebreak` triggers a debug pause, in the same fashion as a hardware breakpoint
  * `fence` is conformant for device input and memory reads and memory writes, but not for all cases of device output
* `M` Extension for Integer Multiplication and Division, Version 2.0
* "Zicsr" Extension for Control and Status Register (CSR) Instructions, Version 2.0
* "Zaamo" Extension for Atomic Memory Operations (part of `A` Extension for Atomic Instructions, Version 2.1)
* "Zba" Extension for Address Generation (part of `B` Extension for Bit Manipulation, Version 1.0.0)
* "Zbb" Extension for Basic Bit Manipulation (part of `B` Extension for Bit Manipulation, Version 1.0.0)
* Miscellaneous additional bit manipulation instructions:
  * `pack` and `brev8` as specified in "Zbkb"
  * `grevi` as specified in Bitmanip 0.94-draft
* `.ttinsn` for efficiently pushing static instructions to the Tensix coprocessor (implemented everywhere, but only useful in babies which actually have Tensix coprocessor instruction push in their address space)
* Some, but not all, of "Zicntr" Extension for Base Counters and Timers, Version 2.0:
  * `cycle` and `instret` are available, but `time` is not
* Some, but not all, of `F` Extension for Single-Precision Floating-Point, Version 2.2:
  * Rounding mode bits are ignored; rounding mode is always RNE (round to nearest, ties to even)
  * `fflags` and `frm` CSRs are not implemented (though `fcsr` _is_, and contains all the relevant bits)
  * `fdiv.s` and `fsqrt.s` are not implemented
  * `fadd.s` / `fsub.s` / `fmul.s` _are_ implemented, but treat denormal inputs as zero, and flush denormals to zero on output
  * `fmadd.s` / `fmsub.s` / `fnmsub.s` / `fnmadd.s` _will execute_, but treat denormal inputs as zero, and flush denormals to zero on output, [and have semantics somewhere between separate multiply/add and fused multiply/add](../../../Miscellaneous/FMA/README.md) (i.e. even aside from denormals, they do _not_ conform to the IEEE754 definition of fused multiply/add)
* Some, but not all, of "Zfh" Extension for Half-Precision Floating-Point, Version 1.0:
  * Same caveats as for `F` Extension above (for `.h` instructions as for `.s` instructions)
  * Also a custom CSR bit for switching these instructions to operate on BF16 values rather than FP16 values
* RISCV B and RISCV NC only: `mret` instruction for returning from interrupt handlers (encoded as per Machine-Level ISA, Version 1.13, but privilege levels as described therein are not implemented)
* RISCV T2 only: some, but not all, of `V` Standard Extension for Vector Operations, Version 1.0:
  * Vector registers are 16 bytes wide, maximum element length is 32 bits
  * `vcsr` CSR is not implemented (though `vxsat` and `vxrm` _are_, and contain the relevant bits)
  * The `vta` and `vma` bits of `vtype` are ignored (this is conformant, as execution always has semantics of "undisturbed")
  * No `vill` bit in `vtype`
  * `vdiv` / `vdivu` / `vrem` / `vremu` are not implemented (i.e. no integer division / remainder)
  * `vfdiv` / `vfrdiv` / `vfsqrt` / `vfrsqrt7` / `vfrec7` are not implemented (i.e. no floating-point division / square root / reciprocal)
  * Floating-point vector instructions have the same caveats as their scalar equivalents regarding rounding, denormals, and [FMA semantics](../../../Miscellaneous/FMA/README.md).

## Base Instruction Set

The majority of base instructions do not require any commentary, as they conform to the standard and the standard exactly ties down their semantics. The minor exceptions are `ebreak` and `fence`.

`ebreak` triggers a debug pause, in the same fashion as a hardware breakpoint. This will pause the RISCV core, and requires some other entity (for example a debugger running on the host) to resume it.

`fence` instructions are specified as having eight mode bits: `PI`, `PO`, `PR`, `PW`, `SI`, `SO`, `SR`, `SW`. The babies ignore these bits, and instead execute all `fence` instructions in the strongest possible manner supported by the hardware:
* Once a `fence` instruction leaves the frontend, the next instruction will not leave the frontend until the `fence` instruction has retired (though the `DisCsrSync` CSR configuration bit can disable this).
* A `fence` instruction will not enter the Load/Store Unit until the store queue has been drained and all in-flight loads have determined their result value (even if that value hasn't yet been committed to the register file).
* When a `fence` instruction enters the Load/Store Unit, the entire L0 data cache will be flushed.
* As is the case for all memory instructions, `fence` instructions leave the frontend in program order, enter the Load/Store Unit in program order, and retire in program order.

The gap in `fence` semantics is stores: ensuring that the store queue has been drained merely ensures that the write requests have been sent on their way to their final destination - it doesn't ensure that the write requests have actually reached their final destination and been processed. In particular, reordering can still occur between two requests if they target two different memory regions.

## Zicsr

See [CSRs](CSRs.md) for details of the implemented CSRs.

## Zaamo

"Zaamo" provides `amoadd.w`, `amoswap.w`, `amoxor.w`, `amoor.w`, `amoand.w`, `amomin.w`, `amomax.w`, `amominu.w`, and `amomaxu.w` instructions. These instructions can only target the local L1; they cannot be used against MMIO memory locations, nor against the address space of other tiles (though note that the NoC can be instructed to perform atomic operations on the L1 of remote tiles within the same ASIC). These instructions have `aq` and `rl` bits, but the exact value of these bits has no effect: the babies always execute these instructions as if both bits were set (which is conformant with the specification, though comes with a slight performance cost).

The `A` Extension for Atomic Instructions, Version 2.1, is comprised of "Zalrsc" and "Zaamo". The babies only implement "Zaamo"; they do _not_ implement "Zalrsc" (i.e. there is no support for load-reserved `lr.w` nor store-conditional `sc.w`).

## Bit Manipulation

"Zba" provides three instructions: `sh1add`, `sh2add`, and `sh3add`.

"Zbb" provides a variety of bit manipulation instructions: `andn`, `clz`, `cpop`, `ctz`, `max`, `maxu`, `min`, `minu`, `orc.b`, `orn`, `rev8`, `rol`, `ror`, `rori`, `sext.b`, `sext.h`, `xnor`, and `zext.h`.

The `B` Extension for Bit Manipulation, Version 1.0.0, is comprised of "Zba" and "Zbb" and "Zbs". The babies implement "Zba" and "Zbb", but they do not implement "Zbs". The same chapter of the standard defines some additional extensions, which the babies mostly do not implement:
* "Zbc" (carry-less multiplication): not implemented.
* "Zbkb" (cryptography): partially overlaps with "Zbb", so the overlapping instructions are implemented (`rol`, `ror`, `rori`, `andn`, `orn`, `xorn`, `rev8`). Of the remaining instructions, `pack` and `brev8` _are_ implemented, but `packh` and `zip` and `unzip` are _not_.
* "Zbkc" (carry-less multiplication for cryptography): not implemented.
* "Zbkx" (crossbar permutations): not implemented.

The `B` Extension for Bit Manipulation, Version 1.0.0, does _not_ specify a `grevi` instruction. Such an instruction was present in draft versions of the standard (such as Bitmanip 0.94-draft), but did not make it to version 1.0.0 of the standard; the babies implement it as per the draft version. It provides a superset of the functionality of `rev8` and `brev8`. For the avoidance of doubt, there are some other instructions which didn't make it to version 1.0.0 of the standard, but `grevi` is the only such instruction implemented by the babies.

## `V` Standard Extension for Vector Operations

RISCV T2 has some, but not all, of `V` Standard Extension for Vector Operations, Version 1.0. See [camel-cdr's RVV benchmark](https://camel-cdr.github.io/rvv-bench-results/tt_t2/index.html) for performance measurements. Some high-level findings from that benchmark include:
* Most vector instructions suffer from a false dependency bug on the destination register (as if the instruction was masked with undisturbed semantics). This is especially visible when LMUL is 8, as then the dependency is on the entire destination group.
* Fractional LMUL suffers from a false dependency bug (as if LMUL was greater than one), meaning that careful register assignment is required to avoid poor performance when using fractional LMUL.
* LMUL greater than one is handled by splitting an instruction into multiple micro-ops.
* Some instructions are then split _again_ into one micro-op per element.
