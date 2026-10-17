# Miscellaneous Unit

The Miscellaneous Unit can perform instructions to manipulate ADCs, along with a few other instructions that don't fit anywhere else. All Miscellaneous Unit instructions execute in a single cycle, and the Miscellaneous Unit can accept one instruction per thread per cycle.

The ADC manipulation instructions are:

|Dimension|Set Instructions|Increment Instruction|CR Increment Instruction|
|---:|---|---|---|
|**X**|[`SETADC`](SETADC.md), [`SETADCXY`](SETADCXY.md), [`SETADCXX`](SETADCXX.md)|[`INCADCXY`](INCADCXY.md)|[`ADDRCRXY`](ADDRCRXY.md)|
|**Y**|[`SETADC`](SETADC.md), [`SETADCXY`](SETADCXY.md)|[`INCADCXY`](INCADCXY.md)|[`ADDRCRXY`](ADDRCRXY.md)|
|**Z**|[`SETADC`](SETADC.md), [`SETADCZW`](SETADCZW.md)|[`INCADCZW`](INCADCZW.md)|[`ADDRCRZW`](ADDRCRZW.md)|
|**W**|[`SETADC`](SETADC.md), [`SETADCZW`](SETADCZW.md)|[`INCADCZW`](INCADCZW.md)|[`ADDRCRZW`](ADDRCRZW.md)|

Note that the Scalar Unit (ThCon) instruction [`REG2FLOP`](REG2FLOP_ADC.md) can move values from GPRs to ADCs.

The instructions which live in the Miscellaneous Unit because they don't fit anywhere else are:
* [`SETDVALID`](SETDVALID.md)
* [`NOP`](NOP.md)
