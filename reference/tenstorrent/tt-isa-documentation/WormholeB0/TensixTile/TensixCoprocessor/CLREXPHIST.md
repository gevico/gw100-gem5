# `CLREXPHIST` (Reset packer exponent histograms)

**Summary:** Resets the [exponent histogram](Packers/ExponentHistogram.md) of all four packers.

**Backend execution unit:** [Matrix Unit (FPU)](MatrixUnit.md)

## Syntax

```c
TTI_CLREXPHIST
```

## Encoding

![](../../../Diagrams/Out/Bits32_CLREXPHIST.svg)

## Functional model

Invokes [the `Reset` function](Packers/ExponentHistogram.md#functional-model) on the exponent histogram of all four packers.
