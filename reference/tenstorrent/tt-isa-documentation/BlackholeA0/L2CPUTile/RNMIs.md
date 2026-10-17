# Resumable non-maskable interrupts (RNMIs)

The "Smrnmi" extension is _not_ implemented, but something very similar _is_ implemented, with the major differences to the standard extension being:
* When coming out of reset, `mnstatus.NMIE` holds the value `1` (i.e. RNMIs enabled) rather than `0`.
* There is no affordance for overriding `mstatus.MPRV` when `mnstatus.NMIE` is `0`.
* The CSR numbers are `0x350` through `0x353` rather than `0x740` through `0x744`.

For readers not familiar with "Smrnmi", a complete description follows.

RNMIs are the highest priority of interrupt. They are always enabled, and can interrupt any kind of execution other than RNMI handlers themselves.

## RNMI Causes

There are two possible causes of RNMIs:
* The hart's trigger bit within `0x0000_2001_0414` being set to `true`. This RNMI cause is level-triggered rather than edge-triggered, and is reported by hardware setting the low 63 bits of `mncause` to the value `2`.
* The hart's bus error unit (BEU) being configured to raise a local interrupt in response to particular erroneous events, and such an event being set to `true` in the BEU's accrued event mask. This RNMI cause is level-triggered rather than edge-triggered, and is reported by hardware setting the low 63 bits of `mncause` to the value `3`.

If one of the above causes happens, and `mnstatus.NMIE == 1`, hardware will:
* Set `mnstatus.NMIE` to `0`.
* Write the current privilege level to `mnstatus.MNPP` and then set the current privilege level to machine.
* Write the current `pc` to `mnepc` and then set `pc` to the hart's RNMI trap handler address.
* Populate `mncause` based on the particular cause.

## RNMI Handlers

An RNMI handler is responsible for handling an RNMI in an application-specific manner. An RNMI handler cannot be interrupted, although if an exception is encountered whilst executing an RNMI handler, control flow will jump to the hart's RNMI exception trap handler address.

If an RNMI handler wishes to use GPRs, it can free up one GPR by writing it to `mnscratch` and then restoring it from `mnscratch` before completing. If it requires more than one GPR (which is likely), the first GPR can be used to form an address to a suitable location in memory, and then subsequent GPRs can be saved off to that address and later restored. Software may need to take different codepaths depending on whether `mstatus.MPRV` is set or clear.

An RNMI handler can complete in one of two ways:
* By executing an `mnret` instruction (encoding `0x70200073`). This will set `pc` to `mnepc`, set the privilege level to `mnstatus.MNPP`, and set `mnstatus.NMIE` to `1`.
* By executing an appropriate CSR instruction to set `mnstatus.NMIE` to `1`. In this case, execution will continue at the next instruction, and the privilege level will remain machine mode.

Note that all RNMI causes are level-triggered rather than edge-triggered, so unless the handler has done something to actually address the root cause of the RNMI, the hart will suffer another RNMI after the handler completes.

## Memory Map

RNMI trigger bits exist as part of a 32-bit field at `0x0000_2001_0414`:

|First&nbsp;bit|#&nbsp;Bits|Purpose|
|--:|--:|---|
|0|1|RNMI trigger for hart 0|
|1|1|RNMI trigger for hart 1|
|2|1|RNMI trigger for hart 2|
|3|1|RNMI trigger for hart 3|
|4|28|Reserved|

RNMI trap handler addresses exist as an array starting at `0x0000_2001_0418`:

|Address (x280 physical)|Size|Contents|
|---|--:|---|
|`0x0000_2001_0418`|47 bits|Hart 0 RNMI trap handler address|
|`0x0000_2001_0420`|47 bits|Hart 0 RNMI exception trap handler address|
|`0x0000_2001_0428`|47 bits|Hart 1 RNMI trap handler address|
|`0x0000_2001_0430`|47 bits|Hart 1 RNMI exception trap handler address|
|`0x0000_2001_0438`|47 bits|Hart 2 RNMI trap handler address|
|`0x0000_2001_0440`|47 bits|Hart 2 RNMI exception trap handler address|
|`0x0000_2001_0448`|47 bits|Hart 3 RNMI trap handler address|
|`0x0000_2001_0450`|47 bits|Hart 3 RNMI exception trap handler address|

It is strongly recommended that the various RNMI trap handler addresses and RNMI exception trap handler addresses are set to their intended values prior to taking harts out of reset, and then not subsequently modified.

Relevant CSR addresses are:

|Address&nbsp;(CSR)|Name|Notes|
|---|---|---|
|`0x350`|`mnscratch`|General purpose scratch register for RNMI handlers to use as they see fit.|
|`0x351`|`mnepc`|Upon suffering an RNMI, hardware does `mnepc = pc`, and upon executing `mnret`, hardware does `pc = mnepc`. Software can also arbitrarily change `mnepc` if it wishes.|
|`0x352`|`mncause`|Upon suffering an RNMI or an exception in an RNMI handler, hardware assigns a value to `mncause` indicating the cause.|
|`0x353`|`mnstatus`|Container for `MNIE` and `MNPP`.|

The bit layout of `mncause` is:

|First&nbsp;bit|#&nbsp;Bits|Meaning|
|--:|--:|---|
|0|63|When bit 63 is set:<ul><li><code>2</code>: Cause was per-hart RNMI trigger bit</li><li><code>3</code>: Cause was BEU local interrupt</li></ul><br/>When bit 63 is clear, an exception code.|
|63|1|<ul><li><code>0</code>: Cause was exception in RNMI handler</li><li><code>1</code>: Cause was RNMI</li></ul>|

The bit layout of `mnstatus` is:

|First&nbsp;bit|#&nbsp;Bits|Name|Meaning|
|--:|--:|---|---|
|0|3|Reserved|Always zero|
|3|1|`MNIE`|<ul><li><code>0</code>: RNMI handler executing, and thus cannot suffer any kind of interrupt</li><li><code>1</code>: RNMI handler not executing, and thus RNMIs enabled (other types of interrupt may or may not be enabled, subject to `mstatus.MIE` et al.)</li></ul>Note that software can transition this bit from `0` to `1`, but only hardware is capable of transitioning it from `1` to `0`.|
|4|7|Reserved|Always zero|
|11|2|`MNPP`|<ul><li><code>0</code>: User mode</li><li><code>1</code>: Supervisor mode</li><li><code>2</code>: Reserved</li><li><code>3</code>: Machine mode</li></ul>|
|13|19|Reserved|Always zero|
