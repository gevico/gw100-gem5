# L2CPU Tile

Each L2CPU tile contains a coherent cluster of four SiFive x280 CPUs, along with various [caches](Caches.md). Each L2CPU tile has a direct connection to a local DRAM tile at fixed location in its [address space](MemoryMap.md), and then has [256 TLB windows to the NoC](TLBWindows.md), which allow configurable access to any other tile on the [NoC](../NoC/README.md), notably including access to other DRAM tiles and access to the host through the [PCI Express tile](../PCIExpressTile/README.md).

> [!TIP]
> The SiFive x280 CPUs found in Blackhole L2CPU tiles have no relation to the [Ascalon CPUs](https://tenstorrent.com/en/ip/tt-ascalon) being designed by Tenstorrent, other than both being 64-bit RISCV CPUs. Some future Tenstorrent products are likely to include Ascalon CPUs, but Blackhole contains SiFive x280 CPUs.

## Block Diagrams

This tile is slightly too complex to show on a single diagram, though most components are shown on both diagrams.

### x280 to NoC and DRAM

![](../../Diagrams/Out/EdgeTile_BH_L2CPU_H2D.svg)

### NoC to x280

![](../../Diagrams/Out/EdgeTile_BH_L2CPU_D2H.svg)

### Diagram notes

* Thick arrows are used to show the primary data paths, whereas thin arrows are used to show configuration paths.
* The destination of the "To DRAM tile" arrow varies based on the L2CPU tile: CPUs 0-3 connect to tile D5, CPUs 4-7 connect to D6, and then both of CPUs 8-11 and CPUs 12-15 connect to D7 (which they need to appropriately share).
* Protocol conversions are implicit whenever the width or colour of an arrow changes.
* The L1D and L1I caches are shown as sitting before the MMU, though this is a slight misrepresentation, as in practice they are virtually-indexed and physically-tagged.

## Reset

> [!CAUTION]
> Due to a hardware bug, the harts within each L2CPU tile can only be brought out of reset once. Once running, putting them back into reset requires resetting the entire Blackhole ASIC (e.g. with `tt-smi -r`). As such, software is encouraged to build a mechanism for seizing control away from a running hart and parking it in an idle state in machine mode. One viable approach is to use [RNMIs](RNMIs.md), with the RNMI trap handler set to the external peripherals general purpose scratch memory, and said memory populated with code to execute `fence.i` and then load the per-hart reset handler address and jump to it.

> [!CAUTION]
> The harts within each L2CPU tile should be brought out of reset with the L2SYS clock set to a low speed. See [tt-bh-linux's `clock.py`](https://github.com/tenstorrent/tt-bh-linux/blob/c1484a0f0f10fa35c8c7cb4e33b49ba6d2a5e0d2/clock.py) for an example of how to do this, which involves directly manipulating the PLLs via MMIO addresses in the ARC tile. Once out of reset, the clock speed can be raised.

The harts within an L2CPU tile are brought out of reset by transitioning a bit in the ARC tile's `L2CPU_RESET` register from `0` to `1`. When accessed through the NoC, `L2CPU_RESET` exists at address `0x80030014` within the ARC tile, with contents:

|First&nbsp;bit|#&nbsp;Bits|Purpose|
|--:|--:|---|
|0|4|Reserved; software should not modify|
|4|1|Reset bit for CPUs 0-3 (L2CPU tile at NoC #0 coordinates X=8,Y=3)|
|5|1|Reset bit for CPUs 4-7 (L2CPU tile at NoC #0 coordinates X=8,Y=9)|
|6|1|Reset bit for CPUs 8-11 (L2CPU tile at NoC #0 coordinates X=8,Y=5)|
|7|1|Reset bit for CPUs 12-15 (L2CPU tile at NoC #0 coordinates X=8,Y=7)|
|8|24|Reserved; writes ignored, reads as zero|

Prior to bringing a tile's harts out of reset, all harts should have their [reset handler address](MemoryMap.md#external-peripherals) set to their desired initial `pc`.

After coming out of reset, software is likely to want to:
* Write zero to CSR `0x7c1` (this clears all feature-disable chicken bits).
* Configure the [L2 cache prefetchers](Caches.md#l2-cache) by writing `0x15811` to `L2PF1_BASIC_CONTROL` and `0x38c84e` to `L2PF1_USER_CONTROL` (this can alternatively be done by the host).
* Configure the [L3 cache](Caches.md#l3-cache) to be 2 MiB in size and L3 as uncached scratchpad (LIM) to be zero bytes in size, by writing `15` to `CCACHE0_WAYENABLE` (this can alternatively be done by the host).
