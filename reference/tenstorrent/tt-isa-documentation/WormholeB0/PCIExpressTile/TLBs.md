# PCI Express Tile Configurable TLBs

There are 186 configurable TLB windows, which collectively occupy the low 496 MiB of bar 0. The first 156 windows are each 1 MiB in size, the next 10 windows are each 2 MiB in size, and the final 20 windows are each 16 MiB in size. Each window can be pointed at an aligned region of the address space of any tile in the NoC. The final 16 MiB window is reserved for use by the kernel driver, with all the remaining windows available for use by customer software.

## Configuration

Each TLB window has 64 bits of configuration associated with it. The configuration for all 186 windows is concatenated to form an array `uint64_t config[186];`, which starts at address `0x1FC0_0000` in bar 0 or `0x01C0_0000` in bar 4. The first 156 entries correspond to the 156x 1 MiB windows, the next 10 entries to the 10x 2 MiB windows, and the final 20 entries to the 20x 16 MiB windows. Recent versions of [tt-kmd](https://github.com/tenstorrent/tt-kmd/) provide `TENSTORRENT_IOCTL_ALLOCATE_TLB` / `TENSTORRENT_IOCTL_CONFIGURE_TLB` / `TENSTORRENT_IOCTL_FREE_TLB` ioctls for software wishing to delegate the details to the kernel driver. If software does its own TLB management rather than delegating to the kernel driver, it still needs to ignore the final 16 MiB TLB, as the kernel driver uses it internally.

The fields within each 64 bits are:

|First&nbsp;bit|#&nbsp;Bits|Name|Purpose|
|--:|--:|---|---|
|0|N|`local_offset`|When a TLB is accessed, hardware needs to form a 36-bit address within the target tile(s). The high `N` bits of that address come from this `local_offset`, and the low `36 - N` bits come from the accessed offset within the TLB.<br/>For 1 MiB TLBs, `N == 16`, as 20 bits come from the offset within the TLB.<br/>For 2 MiB TLBs, `N == 15`, as 21 bits come from the offset within the TLB.<br/>For 16 MiB TLBs, `N == 12`, as 24 bits come from the offset within the TLB.|
|N|6|`x_end`|When `mcast` is set, the [X coordinate](../NoC/Coordinates.md) of the end of the multicast rectangle. Otherwise the X coordinate of the single target tile.|
|N + 6|6|`y_end`|When `mcast` is set, the [Y coordinate](../NoC/Coordinates.md) of the end of the multicast rectangle. Otherwise the Y coordinate of the single target tile.|
|N + 12|6|`x_start`|When `mcast` is set, the [X coordinate](../NoC/Coordinates.md) of the start of the multicast rectangle. Ignored otherwise.|
|N + 18|6|`y_start`|When `mcast` is set, the [Y coordinate](../NoC/Coordinates.md) of the start of the multicast rectangle. Ignored otherwise.|
|N + 24|1|`noc_sel`|`0` to select NoC #0, `1` to select NoC #1.|
|N + 25|1|`mcast`|Equivalent to `NOC_CMD_BRCST_PACKET`; `true` causes `(x_start, y_start)` through `(x_end, y_end)` to specify a rectangle of target Tensix tiles, whereas `false` causes `(x_end, y_end)` to specify a single tile target.|
|N + 26|2|`ordering`|Three possible [ordering modes](#ordering-modes):<ul><li>`0` - Default</li><li>`1` - Strict AXI</li><li>`2` - Posted Writes</li></ul>|
|N + 28|1|`linked`|Similar to [`NOC_CMD_VC_LINKED`](../NoC/MemoryMap.md#noc_ctrl). It is never safe to set this to `true`, as the kernel driver reserves the right to use its TLB window at any time, and _it_ always has `linked` set to `false`.|
|N + 29|1|`static_vc`|Equivalent to [`NOC_CMD_VC_STATIC`](../NoC/MemoryMap.md#noc_ctrl)|
|N + 30|34 - N|Reserved|Software can write to these bits, and read the value back, but they have no effect on the TLB|

The mapping of these bits to [NIU request initiator fields](../NoC/MemoryMap.md#niu-request-initiators) is:

|Field(s)|Contents|
|---|---|
|`NOC_TARG_ADDR_LO` and `NOC_TARG_ADDR_MID`|For host reads: the X/Y and address inferred from a combination of the TLB configuration and the offset of the read within the TLB window.<br/>For host writes: the X/Y of the PCI Express tile, and an appropriately aligned internal address which will cause the data to be that written by the host.|
|`NOC_RET_ADDR_LO` and `NOC_RET_ADDR_MID`|For host writes: the X/Y and address inferred from a combination of the TLB configuration and the offset of the write within the TLB window.<br/>For host reads: the X/Y of the PCI Express tile, and an appropriately aligned internal address which will cause the data to be returned to the host.|
|`NOC_PACKET_TAG`|All zero|
|`NOC_CTRL` request type|`NOC_CMD_RD` for host reads, `NOC_CMD_WR` for host writes|
|`NOC_CTRL.NOC_CMD_WR_BE`|`false`|
|`NOC_CTRL.NOC_CMD_WR_INLINE`|`false`|
|`NOC_CTRL.NOC_CMD_RESP_MARKED`|`false` when the TLB ordering mode is "Posted writes", `true` otherwise|
|`NOC_CTRL.NOC_CMD_BRCST_PACKET`|`mcast` from the TLB configuration|
|`NOC_CTRL.NOC_CMD_VC_LINKED`|`linked` from the TLB configuration|
|`NOC_CTRL.NOC_CMD_VC_STATIC`|`static_vc` from the TLB configuration|
|`NOC_CTRL.NOC_CMD_PATH_RESERVE`|`false`|
|`NOC_CTRL.NOC_CMD_MEM_RD_DROP_ACK`|N/A|
|`NOC_CTRL.NOC_CMD_STATIC_VC` buddy bit|`0` for host writes, `1` for host reads|
|`NOC_CTRL.NOC_CMD_STATIC_VC` class bits|`0b10` for broadcast writes, `0b00` otherwise|
|`NOC_CTRL.NOC_CMD_BRCST_XY`|`0`|
|`NOC_CTRL.NOC_CMD_BRCST_SRC_INCLUDE`|`false`|
|`NOC_CTRL.NOC_CMD_ARB_PRIORITY`|`0`|
|`NOC_AT_LEN_BE`|The length of the host read or write|

## Ordering modes

PCI Express defines some rules regarding what reorderings are permitted, and the three possible TLB ordering modes provide varying levels of conformance to those rules:

|Scenario (†)|PCI Express Rules|Strict AXI|Default|Posted Writes|
|---|---|---|---|---|
|Read then write|Reordering possible|Conforms to PCIe|Conforms to PCIe|Conforms to PCIe|
|Read then read|Reordering possible|Stronger than PCIe|Conforms to PCIe|Conforms to PCIe|
|Write then read|Order maintained|Conforms to PCIe|Conforms to PCIe|Reordering possible|
|Write then write|Order maintained|Conforms to PCIe|Dependent on other flags|Dependent on other flags|

> (†) Technically the PCI Express rules are specified in terms of posted transactions (P) and non-posted transactions (NP), but for sake of exposition it is reasonable to replace "posted transaction" with "write" and "non-posted transaction" with "read".

### Strict AXI

In this mode, PCI Express ordering rules are followed, as are AXI ordering rules. Following all of these rules severely limits how many NoC transactions the PCI Express tile can have in flight at any time.

### Default

In this mode, most PCI Express ordering rules are followed, except that writes can reorder or interleave with other writes after they've passed through the NIU in the PCI Express tile. If the `static_vc` flag is set on the TLB, then ordering is maintained all the way through to the NIU in the recipient tile, and the recipient NIU will maintain ordering for each individual byte address. Two caveats remain:
1. If the TLB is subsequently reconfigured to point at different target tile(s), writes from before the reconfiguration can still reorder with writes from after the reconfiguration, as `static_vc` can only enforce ordering when the path remains the same. Note that this risk only applies to packets once they are on the NoC; the reconfiguration process itself is safe (i.e. writes from before the reconfiguration will use the old configuration, and writes from after the reconfiguration will use the new configuration).
2. The recipient NIU does not guarantee to _always_ maintain ordering between writes to different addresses. See [NoC Ordering](../NoC/Ordering.md#ordering-properties-of-two-packets) for a description of the provided guarantees.

### Posted Writes

In this mode, the PCI Express tile will return an AXI write acknowledgement for write requests as soon as the write data has entered the NoC. This can cause the PCI Express Controller to violate the "Write then read" ordering requirement of PCI Express, as the controller can emit the read request as soon as it receives the write acknowledgement, and then the read request can _potentially_ reorder with the write request as they both travel on the NoC. Note that `static_vc` cannot be used to mitigate this, as the PCI Express tile uses different static VCs for reads versus writes.

For "Write then write", this mode behaves as per "Default".
