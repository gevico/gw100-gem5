# `NOP` (Do nothing)

**Summary:** Does nothing other than delaying the issuing thread's next Tensix instruction by one cycle. Note that this Tensix `NOP` instruction is not the same as a RISCV `nop` instruction.

**Backend execution unit:** [Miscellaneous Unit](MiscellaneousUnit.md)

## Syntax

```c
TTI_NOP
```

## Encoding

![](../../../Diagrams/Out/Bits32_NOP.svg)

## Functional model

```c
// Causes no effects
```

## Performance

This instruction executes in a single cycle.
