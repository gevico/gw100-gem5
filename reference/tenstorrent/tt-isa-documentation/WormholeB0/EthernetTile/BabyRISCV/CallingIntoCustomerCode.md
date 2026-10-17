# Calling into customer code

Customer software has complete control over Tensix tiles, so it can assume control by putting the relevant RISCV cores into [soft reset](../../TensixTile/SoftReset.md#riscv-soft-reset) and then taking them out of reset at a known `pc`. Doing the same thing on Ethernet tiles is disruptive to the Tenstorrent code running on the RISCV core, so a different mechanism is preferred.

To request that the on-device Tenstorrent code call some on-device customer code, customer host software should:
1. Read the four bytes in L1 at `0x9004`. If this value is non-zero, then some customer code is likely still running on the device, and host software should not proceed.
2. Ensure that the machine code it wishes to execute is present on the device, with the first instruction of that code present at address `0x9040`.
3. Write a non-zero value to the four bytes in L1 at `0x9004`. Once the Tenstorrent code observes this, it will perform a RISCV function call to the function whose machine code starts at `0x9040`.

Once the customer code has finished doing what it wants to do, it should:
1. Write a zero value to the four bytes in L1 at `0x9004`.
2. Execute a RISCV `ret` instruction (e.g. `jalr x0, ra`).

The usual RISCV ABI needs to be followed, meaning that:
1. Upon function entry, `sp` will be set and be 16-byte aligned, and software can decrement `sp` by some multiple of 16 bytes if it needs to allocate stack space (provided that it increments it again before returning).
2. Upon function entry, `ra` will be set. Software should return to this address when it is done. Note that there is no return address stack within the branch predictor.
3. The `gp` and `tp` registers should be considered as constants, and their values not changed.
4. If any of `s0` through `s11` are used, their values need to be saved prior to being used, and their values restored prior to returning.

If the customer code is long running, it also needs to occasionally yield control back to the Tenstorrent code. This is done by:
1. Reading four bytes in L1 at `0x9020`.
2. Calling the function pointer obtained by the previous read. In addition to all the usual ABI guarantees, this function also guarantees to preserve all of `t0` through `t6` and all of `a0` through `a7`. This means that the only register perturbed by the call is `ra`. That said, the call may perturb other device state; notably it can do arbitrary things to the [NIUs](../../NoC/MemoryMap.md) and to the [Ethernet transmit subsystem](../EthernetTxRx.md). If it initiates any NoC requests, then it will wait for all the relevant counter updates to occur before returning to customer code.
