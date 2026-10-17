# Auto TTSync

Once enabled (by using a `SETC16` instruction to set `TENSIX_TRISC_SYNC_TrackTensixInstructions` to `true` and one or more of `TENSIX_TRISC_SYNC_TrackGPR` / `TENSIX_TRISC_SYNC_TrackTDMARegs` / `TENSIX_TRISC_SYNC_TrackGlobalCfg` to `true`), Auto TTSync makes the programming model slightly easier by ensuring that some common patterns seen in RISCV T0 / T1 / T2 code are race-free by default, rather than requiring explicit programmer care to make them race-free.

## Tracked resources

Eight separate resources are tracked:
* Tensix Backend Configuration, tracked as six separate pieces:
  * The subset of `Config[0]` which applies to unpacker 0
  * The subset of `Config[0]` which applies to unpacker 1
  * The remainder of `Config[0]`
  * The subset of `Config[1]` which applies to unpacker 0
  * The subset of `Config[1]` which applies to unpacker 1
  * The remainder of `Config[1]`
* Tensix GPRs
* TDMA-RISC state

Note that there is no sub-resource tracking (e.g. of individual GPRs or individual configuration fields). This can lead to spurious stalls, which are _safe_, but can impact performance.

## Automatically handled scenarios

### Load from Tensix Backend Configuration / Tensix GPRs / TDMA-RISC followed by store to push Tensix instruction

If the pushed Tensix instruction writes to a resource which RISCV loads from, then the Tensix instruction will wait at the Wait Gate until the RISCV load has determined its value, thereby maintaining the illusion of events happening in program order: the value observed by RISCV will be from _before_ the Tensix instruction executes.

If the pushed Tensix instruction is `MOP`, then the first instruction resulting from the MOP expansion will wait at the Wait Gate using the resource declaration of the `MOP` instruction, and subsequent instructions from the expansion are immune to Auto TTSync. Otherwise, if the pushed Tensix instruction is `REPLAY`, then the first instruction resulting from the replay expansion will wait at the Wait Gate using the resource declaration of the `REPLAY` instruction, and subsequent instructions from the expansion are immune to Auto TTSync. Other types of Tensix instruction do not change between being pushed and arriving at the Wait Gate.

### Store to Tensix Backend Configuration / Tensix GPRs / TDMA-RISC followed by store to push Tensix instruction

If the pushed Tensix instruction reads or writes a resource which RISCV stores to, then the Tensix instruction will wait at the Wait Gate until the RISCV store has happened, thereby maintaining the illusion of events happening in program order:
* If the Tensix instruction reads the resource, it'll observe the value stored by RISCV.
* If the Tensix instruction writes the resource, the final value of the resource will be the write from the Tensix instruction.

If the pushed Tensix instruction is `MOP`, then the first instruction resulting from the MOP expansion will wait at the Wait Gate using the resource declaration of the `MOP` instruction, and subsequent instructions from the expansion are immune to Auto TTSync. Otherwise, if the pushed Tensix instruction is `REPLAY`, then the first instruction resulting from the replay expansion will wait at the Wait Gate using the resource declaration of the `REPLAY` instruction, and subsequent instructions from the expansion are immune to Auto TTSync. Other types of Tensix instruction do not change between being pushed and arriving at the Wait Gate.

### Store to push Tensix instruction followed by store to Tensix Backend Configuration / Tensix GPRs / TDMA-RISC

If the pushed Tensix instruction reads or writes a resource which RISCV stores to, then the RISCV store will be automatically stalled until the Tensix instruction has passed through the Wait Gate, thereby maintaining the illusion of events happening in program order:
* If the Tensix instruction reads the resource, it'll observe the value from before the RISCV store.
* If the Tensix instruction writes the resource, the final value of the resource will be the write from RISCV.

If the pushed Tensix instruction is `MOP`, the resource declaration comes from the `MOP` instruction rather than any instruction in the expansion, and RISCV will wait for _all_ instructions in the expansion to pass through the Wait Gate. Otherwise, if the pushed Tensix instruction is `REPLAY`, the resource declaration comes from the `REPLAY` instruction rather than any instruction in the expansion, and again RISCV will wait for _all_ instructions in the expansion to pass through the Wait Gate. Other types of Tensix instruction do not change between being pushed and arriving at the Wait Gate.

### Store to push Tensix instruction followed by load from Tensix Backend Configuration / Tensix GPRs / TDMA-RISC

> [!CAUTION]
> Due to a hardware bug, this scenario isn't fully automatic. It requires that the pushed Tensix instruction is drained out of the RISCV store queue before the RISCV load is issued. One way of ensuring this is to execute a RISCV `fence` instruction between the store and the load.

If the pushed Tensix instruction writes a resource which RISCV loads from, then the RISCV load will be automatically stalled until the Tensix instruction has passed through the Wait Gate, thereby maintaining the illusion of events happening in program order: the value observed by RISCV will be from _after_ the Tensix instruction executes.

If the pushed Tensix instruction is `MOP`, the resource declaration comes from the `MOP` instruction rather than any instruction in the expansion, and RISCV will wait for _all_ instructions in the expansion to pass through the Wait Gate. Otherwise, if the pushed Tensix instruction is `REPLAY`, the resource declaration comes from the `REPLAY` instruction rather than any instruction in the expansion, and again RISCV will wait for _all_ instructions in the expansion to pass through the Wait Gate. Other types of Tensix instruction do not change between being pushed and arriving at the Wait Gate.


### Other scenarios

No other scenarios are handled by Auto TTSync. In particular, it does _not_ assist with RISCV access to any of:
* MOP Expander configuration
* Tensix Dst
* Tensix semaphores
* `sfpu_cc` in the `tt_cfg_qstatus` CSR

Furthermore, Auto TTSync does _not_ assist with:
* Dependencies between one Tensix instruction and another Tensix instruction
* Interactions between different RISCV cores and/or different Tensix threads
* Anything done by RISCV B or RISCV NC

## RISCV address to resource mapping

If both of `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_TrackGPR` and `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_TrackTensixInstructions` are true, RISCV access to `GPRs` (at `REGFILE_BASE` and above) counts as access to the Tensix GPRs resource.

If both of `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_TrackTDMARegs` and `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_TrackTensixInstructions` are true, RISCV access to any TDMA-RISC address (at `0xFFB1_1000` and above) counts as access to the TDMA-RISC state resource.

If both of `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_TrackGlobalCfg` and `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_TrackTensixInstructions` are true, RISCV access to Tensix Backend Configuration (at `TENSIX_CFG_BASE` and above) counts as access to one or more of the configuration resources, depending on the exact address.

When `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_EnSubdividedCfgForUnpacr` is `false`, the address mapping is:
* **`Config[0][j]`, with `j < GLOBAL_CFGREG_BASE_ADDR32`:** All three parts of `Config[0]`.
* **`Config[1][j]`, with `j < GLOBAL_CFGREG_BASE_ADDR32`:** All three parts of `Config[1]`.
* **`Config[i][j]`, with `j ≥ GLOBAL_CFGREG_BASE_ADDR32`:** All three parts of both `Config[0]` and `Config[1]`.
* **`ConfigDualWrite`:** All three parts of both `Config[0]` and `Config[1]`.
* **`ThreadConfig`:** All three parts of both `Config[0]` and `Config[1]`.

When `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_EnSubdividedCfgForUnpacr` is `true`, the address mapping is instead:
* **`Config[0][j]`, with `j < GLOBAL_CFGREG_BASE_ADDR32`:** One or more parts of `Config[0]`, as per below table.
* **`Config[1][j]`, with `j < GLOBAL_CFGREG_BASE_ADDR32`:** One or more parts of `Config[1]`, as per below table.
* **`Config[i][j]`, with `j ≥ GLOBAL_CFGREG_BASE_ADDR32`:** "The remainder of `Config[i]`" for both `i`.
* **`ConfigDualWrite[j]`, for any `j`:** "The remainder of `Config[i]`" for both `i`.
* **`ThreadConfig[0].DEST_TARGET_REG_CFG_MATH_Offset`:** "The subset of `Config[i]` which applies to unpacker `j`" for all `i` and `j`.
* **Everything else in `ThreadConfig`:** "The remainder of `Config[i]`" for both `i`

For accesses to `Config[0]` and `Config[1]` with `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_EnSubdividedCfgForUnpacr` set to `true`, the below table describes the mapping from field to resource(s). Note that the table only describes the mapping performed by Auto TTSync; this mapping is _intended_ to match what other parts of the hardware do, but this goal is not always achieved.

|`Config` Field|Unpacker 0|Unpacker 1|Remainder|
|---|:-:|:-:|:-:|
|`THCON_SEC0_REG0_TileDescriptor`|✅ (†)|| (†)|
|`THCON_SEC0_REG2_Context_count`|✅|||
|`THCON_SEC0_REG2_Context_count_non_log2`|✅|||
|`THCON_SEC0_REG2_Context_count_non_log2_en`|✅|||
|`THCON_SEC0_REG2_Disable_zero_compress_cntx[]`|✅|||
|`THCON_SEC0_REG2_Force_shared_exp`|✅|||
|`THCON_SEC0_REG2_Haloize_mode`|✅|||
|`THCON_SEC0_REG2_Metadata_x_end`|✅|||
|`THCON_SEC0_REG2_Out_data_format`|✅|||
|`THCON_SEC0_REG2_Ovrd_data_format`|✅|||
|`THCON_SEC0_REG2_Shift_amount_cntx[]`|✅|||
|`THCON_SEC0_REG2_Throttle_mode`|✅|||
|`THCON_SEC0_REG2_Tileize_mode`|✅|||
|`THCON_SEC0_REG2_Unpack_If_Sel`|✅|||
|`THCON_SEC0_REG2_Unpack_Src_Reg_Set_Upd`|✅|||
|`THCON_SEC0_REG2_Unpack_fifo_size`|✅|||
|`THCON_SEC0_REG2_Unpack_if_sel_cntx[]`|✅|||
|`THCON_SEC0_REG2_Unpack_limit_address`|✅|||
|`THCON_SEC0_REG2_Upsample_and_interleave`|✅|||
|`THCON_SEC0_REG2_Upsample_rate`|✅|||
|`THCON_SEC0_REG3_Base_address`|✅|||
|`THCON_SEC0_REG3_Base_cntx[]_address`|✅|||
|`THCON_SEC0_REG4_Base_cntx[]_address`|✅|||
|`THCON_SEC0_REG5_Dest_cntx[]_address`|✅|||
|`THCON_SEC0_REG5_Tile_x_dim_cntx[]`|✅|||
|`THCON_SEC0_REG7_Offset_address`|✅|||
|`THCON_SEC0_REG7_Offset_cntx[]_address`|✅|||
|`THCON_SEC0_REG7_Unpack_data_format_cntx[]`|✅|||
|`THCON_SEC0_REG7_Unpack_out_data_format_cntx[]`|✅|||
|`THCON_SEC0_REG10_Unpack_fifo_size`|✅|||
|`THCON_SEC0_REG10_Unpack_limit_address`|✅|||
|`THCON_SEC0_REG10_Unpack_limit_address_en`|✅|||
|`THCON_SEC1_REG0_TileDescriptor`||✅ (†)|(†)|
|`THCON_SEC1_REG2_Context_count`||✅||
|`THCON_SEC1_REG2_Context_count_non_log2`||✅||
|`THCON_SEC1_REG2_Context_count_non_log2_en`||✅||
|`THCON_SEC1_REG2_Disable_zero_compress_cntx[]`||✅||
|`THCON_SEC1_REG2_Force_shared_exp`||✅||
|`THCON_SEC1_REG2_Haloize_mode`||✅||
|`THCON_SEC1_REG2_Metadata_x_end`||✅||
|`THCON_SEC1_REG2_Out_data_format`||✅||
|`THCON_SEC1_REG2_Ovrd_data_format`||✅||
|`THCON_SEC1_REG2_Shift_amount_cntx[]`||✅||
|`THCON_SEC1_REG2_Throttle_mode`||✅||
|`THCON_SEC1_REG2_Tileize_mode`||✅||
|`THCON_SEC1_REG2_Unpack_If_Sel`||✅||
|`THCON_SEC1_REG2_Unpack_Src_Reg_Set_Upd`||✅||
|`THCON_SEC1_REG2_Unpack_fifo_size`|||✅|
|`THCON_SEC1_REG2_Unpack_if_sel_cntx[]`||✅||
|`THCON_SEC1_REG2_Unpack_limit_address`|||✅|
|`THCON_SEC1_REG2_Upsample_and_interleave`||✅||
|`THCON_SEC1_REG2_Upsample_rate`||✅||
|`THCON_SEC1_REG3_Base_address`||✅||
|`THCON_SEC1_REG3_Base_cntx1_address`||✅||
|`THCON_SEC1_REG3_Base_cntx[23]_address`|||✅|
|`THCON_SEC1_REG4_Base_cntx[]_address`|||✅|
|`THCON_SEC1_REG5_Dest_cntx[]_address`|||✅|
|`THCON_SEC1_REG5_Tile_x_dim_cntx[]`|||✅|
|`THCON_SEC1_REG7_Offset_address`||✅||
|`THCON_SEC1_REG7_Offset_cntx1_address`||✅||
|`THCON_SEC1_REG7_Offset_cntx[23]_address`|||✅|
|`THCON_SEC1_REG7_Unpack_data_format_cntx[0145]`||✅||
|`THCON_SEC1_REG7_Unpack_data_format_cntx[2367]`|||✅|
|`THCON_SEC1_REG7_Unpack_out_data_format_cntx[0145]`||✅||
|`THCON_SEC1_REG7_Unpack_out_data_format_cntx[2367]`|||✅|
|`UNP0_ADDR_BASE_REG_0_Base`|✅|✅|✅|
|`UNP0_ADDR_BASE_REG_1_Base`|✅|✅|✅|
|`UNP0_ADDR_CTRL_XY_REG_0_Xstride`|✅|||
|`UNP0_ADDR_CTRL_XY_REG_0_Ystride`|✅|||
|`UNP0_ADDR_CTRL_ZW_REG_0_Zstride`|✅|✅||
|`UNP0_ADDR_CTRL_ZW_REG_0_Wstride`|✅|✅||
|`UNP0_ADDR_CTRL_XY_REG_1_Xstride`|||✅|
|`UNP0_ADDR_CTRL_XY_REG_1_Ystride`|||✅|
|`UNP0_ADDR_CTRL_ZW_REG_1_Zstride`|||✅|
|`UNP0_ADDR_CTRL_ZW_REG_1_Wstride`|||✅|
|`UNP0_ADD_DEST_ADDR_CNTR_add_dest_addr_cntr`|✅|✅|✅|
|`UNP0_BLOBS_Y_START_CNTX_01_blobs_y_start`|✅|||
|`UNP0_BLOBS_Y_START_CNTX_23_blobs_y_start`|✅|||
|`UNP0_FORCED_SHARED_EXP_shared_exp`|✅|✅|✅|
|`UNP0_NOP_REG_CLR_VAL_nop_reg_clr_val`|✅|||
|`UNP1_ADDR_BASE_REG_0_Base`||✅||
|`UNP1_ADDR_BASE_REG_1_Base`||✅||
|`UNP1_ADDR_CTRL_XY_REG_0_Xstride`||✅||
|`UNP1_ADDR_CTRL_XY_REG_0_Ystride`||✅||
|`UNP1_ADDR_CTRL_ZW_REG_0_Zstride`||✅||
|`UNP1_ADDR_CTRL_ZW_REG_0_Wstride`||✅||
|`UNP1_ADDR_CTRL_XY_REG_1_Xstride`||✅||
|`UNP1_ADDR_CTRL_XY_REG_1_Ystride`||✅||
|`UNP1_ADDR_CTRL_ZW_REG_1_Zstride`||✅||
|`UNP1_ADDR_CTRL_ZW_REG_1_Wstride`||✅||
|`UNP1_ADD_DEST_ADDR_CNTR_add_dest_addr_cntr`||✅||
|`UNP1_FORCED_SHARED_EXP_shared_exp`||✅||
|`UNP1_NOP_REG_CLR_VAL_nop_reg_clr_val`||✅||
|All other fields|||✅|

> (†) Due to a hardware bug, only the first 32 bits of this field are treated as unpacker. The other 96 bits are treated as remainder.

## Tensix instruction to resource mapping

The below table describes Auto TTSync's default handling of various Tensix instructions. In all cases, `CFG_STATE_ID_StateID` is shorthand for `ThreadConfig[CurrentThread].CFG_STATE_ID_StateID`.

|Class|Instructions|Default mapping|
|--:|---|---|
|N/A|`NOP`, `MOP_CFG`, `RESOURCEDECL`|No resources accessed (hardcoded)|
|0|`ADDDMAREG`, `SUBDMAREG`, `MULDMAREG`, `BITWOPDMAREG`, `SHIFTDMAREG`, `CMPDMAREG`, `SETDMAREG`|Read and write Tensix GPRs|
|1|`ATINCGET`, `ATINCGETPTR`, `ATSWAP`, `ATCAS`|Read and write Tensix GPRs|
|2|`REG2FLOP`|Read and write Tensix GPRs<br/>Read and write TDMA-RISC state|
|3|`LOADIND`, `STOREIND`|Read and write Tensix GPRs|
|4|`STREAMWRCFG`, `CFGSHIFTMASK`|Read&nbsp;and&nbsp;write&nbsp;all&nbsp;three&nbsp;parts&nbsp;of&nbsp;`Config[CFG_STATE_ID_StateID]`|
|5|`STOREREG`|Read Tensix GPRs|
|6|`LOADREG`|Write Tensix GPRs|
|7|`FLUSHDMA`|Write TDMA-RISC state|
|8|`WRCFG`|Read Tensix GPRs<br/>Write all three parts of `Config[CFG_STATE_ID_StateID]`|
|9|`RDCFG`|Read all three parts of `Config[CFG_STATE_ID_StateID]`<br/>Write Tensix GPRs|
|10|`XMOV`|Read and write Tensix GPRs<br/>Read and write all three parts of `Config[CFG_STATE_ID_StateID]`|
|11|`UNPACR`, `UNPACR_NOP`|Read and write TDMA-RISC state<br/>If `ThreadConfig[CurrentThread].TENSIX_TRISC_SYNC_EnSubdividedCfgForUnpacr` is `true`: Read either unpacker 0 or 1 part of `Config[CFG_STATE_ID_StateID]`, based on the instruction's `WhichUnpacker` field<br/>Otherwise: Read all three parts of `Config[CFG_STATE_ID_StateID]`|
|12|`PACR`|Read and write TDMA-RISC state<br/>Read remainder of `Config[CFG_STATE_ID_StateID]`|
|13|`MOP` (and all instructions created by its expansion)|Read and write Tensix GPRs<br/>Read and write TDMA-RISC state<br/>Read and write all three parts of `Config[CFG_STATE_ID_StateID]`|
|14|`REPLAY` (and all instructions created by its expansion)|Read and write Tensix GPRs<br/>Read and write TDMA-RISC state<br/>Read and write all three parts of `Config[CFG_STATE_ID_StateID]`|
|15|All other instructions|Read remainder of `Config[CFG_STATE_ID_StateID]`|

The mapping of instructions to classes is fixed, but the definition of which resources are used by each class can be varied by using the `RESOURCEDECL` instruction. It is _safe_ for Auto TTSync to be told that an instruction uses more resources than it actually does, though this can result in spurious stalls. Each Tensix thread maintains its own resource definitions, and `RESOURCEDECL` always updates the definitions of the executing thread.

Note that `SETC16` is part of class 15, along with most other instructions. Auto TTSync has special handling of `SETC16` when it changes `CFG_STATE_ID_StateID`, but no special handling when it changes any of the `TENSIX_TRISC_SYNC_` fields: after changing any of these fields, software needs to use a mechanism outside of Auto TTSync to wait for the change to take effect, and only then issue Tensix instructions or RISCV loads or stores which rely on the new values.
