# Tensix Tile

Each Tensix tile contains:
  * [1536 KiB of RAM called L1](L1.md)
  * [5x "Baby" RISCV cores](BabyRISCV/README.md)
  * [2x NoC connections](../NoC/README.md) allowing the local RISCV cores to access data in other tiles, and allowing other tiles to access data from this tile
  * [1x NoC overlay](../NoC/Overlay/README.md) - a little coprocessor that can assist with NoC transactions
  * 1x Tensix coprocessor - a big AI coprocessor, itself containing:
    * 2x Unpacker, for moving data from L1 into the coprocessor
    * 1x Matrix Unit (FPU), for performing low precision matrix multiply-accumulate operations and some other matrix operations
    * 1x Vector Unit (SFPU), for performing 32-wide SIMD operations on 32-bit lanes, including FP32 multiply-accumulate operations
    * 1x Scalar Unit (ThCon), for performing integer scalar operations and 128-bit memory operations against L1 (including atomics)
    * 4x Packer, for moving data from the coprocessor back to L1
  * A variety of little utility devices, mainly for the benefit of the RISCV cores: [mailboxes](BabyRISCV/Mailboxes.md), [TDMA-RISC](TDMA-RISC.md), [the debug timestamper](DebugTimestamper.md), and the [PIC](PIC.md).

It is expected that the programmer will provide code for the RISCV cores to execute. High performance will be achieved by having that code oversee the operation of the NoC and the Tensix coprocessor. One very natural division of labour is to have two RISCV cores oversee the NoC and three RISCV cores oversee the Tensix coprocessor. The RISCV cores are not intended to achieve high performance on their own; they are intended to oversee the other components that _actually_ drive performance.

> [!TIP]
> Compared to Wormhole, some of the major upgrades to Tensix tiles in Blackhole are: higher clock speed, more L1 bandwidth, enhanced RISCV instruction set, and a few new Vector Unit (SFPU) instructions.
