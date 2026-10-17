# ARC Tile

The ARC tile contains a four-core [ARC CPU](https://en.wikipedia.org/wiki/ARC_(processor)) for executing firmware, along with access to various peripherals related to chip and board management. Customers can mostly ignore this tile, as it does not execute customer workloads, nor is it involved in dispatching customer workloads. Driver code on the host side sometimes communicates with the ARC CPU for the purpose of:
* Changing the clock speed.
* Obtaining telemetry about the clock speed, device temperature, power usage, and similar.
* Resetting the device without having to reboot the host.
* Upgrading firmware.

## Block Diagrams

This tile is too complex to show on a single diagram, though some components are shown on both diagrams.

### To/from PCI Express and to NoC

![](../../Diagrams/Out/EdgeTile_ARC_H2D.svg)

Adjacent diagrams: [PCI Express host to device](../PCIExpressTile/README.md#host-to-device), [PCI Express device to host](../PCIExpressTile/README.md#device-to-host)

The Fixed TLBs from PCI Express allow PCI Express to access the ARC CSM and the AXI/APB bridge.

### From NoC and to DRAM

![](../../Diagrams/Out/EdgeTile_ARC_D2H.svg)

Adjacent diagrams: [DRAM](../DRAMTile/README.md#block-diagram)

### Connection types

|Arrow style|Protocol|Physical channels|Multiplexing|
|---|---|---|---|
|Thick colored purple/teal|NoC (256b data)|In direction of arrow: single channel carrying all requests / responses / acknowledgements. Arrows collectively form a torus; requests will use the dark colored arrows, responses / acknowledgements will come back on the light colored arrows.|16 virtual channels multiplexed onto each physical channel (12 for requests, 4 for responses).|
|Thick black|AXI|In direction of arrow: read request channel, write request channel, write data channel. In opposite direction: read response channel, write acknowledgement channel.|Many IDs multiplexed onto each physical channel.|
|Thick colored blue|ARC memory|Proprietary information|Proprietary information|
|Thin black|APB (32b data)|In direction of arrow: combined request channel. In opposite direction: combined response / acknowledgement channel.|No|

### Major components

**ARC CPU:** Executes firmware. For Wormhole, the source code for the ARC CPU is unfortunately closed source. However, for the newer Blackhole product line, [the source code for the ARC CPU _is_ available](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/lib/tenstorrent/bh_arc), and it is reasonable to assume that the Wormhole code is doing broadly similar things most of the time.

**ARC CSM:** 512 KiB of RAM, the primary purpose of which is to be the data RAM shared by all four ARC CPU cores, but it can also be accessed via the ARC XBAR.

**ARC XBAR:** Ties together various other components within a 32-bit address space, and then allows various components to access that address space.

**[Reset Unit](ResetUnit.md):** Used to bring the ARC CPU out of reset, and then used by the ARC CPU to bring the rest of the ASIC out of reset. Also home to a few assorted pieces of glue logic that would otherwise be homeless.

**Configurable TLBs:** Exist at an address space boundary, where the target address space is larger than the source address space. Any part of the target address space can be exposed, but the entire target is too big to be simultaneously exposed, so configuration is required to choose which bits of the target to expose. The ARC CPU is responsible for performing the configuration, and then gets exclusive use of the TLBs.

**Fixed TLBs:** Exist to allow PCI Express access to the ARC CSM and the AXI/APB bridge (through which the Reset Unit can be accessed, amongst other things).

**NoC NIUs:** Bidirectional bridge between the AXI protocol and the NoC protocol. Each NIU is connected to a [NoC](../NoC/README.md) router, with the NoC routers connected in a 2D torus spanning the entire ASIC.

### Clock domains

The NoC NIUs straddle the boundary between the AXI clock domain and the AI clock domain. Once in the AI clock domain, there is a single clock domain containing every NoC router and every Tensix tile and the majority of every Ethernet tile.

There are a few components of the ARC tile in the AXI clock domain, but most components are in the ARC clock domain.

A cycle counter exists in the REF clock domain, running at 27 MHz.

## Address spaces

### Host to device, bar 0 / bar 4

|Address range (bar 0)|Address range (bar 4)|Size|Contents|
|---|---|--:|---|
|`0x1FE0_0000`&nbsp;to&nbsp;`0x1FE7_FFFF`|`0x01E0_0000`&nbsp;to&nbsp;`0x01E7_FFFF`|512&nbsp;KiB|Reserved<br/>`0x0000_0000` on the [ARC XBAR](#arc-xbar-4-gib)|
|`0x1FE8_0000` to `0x1FEF_FFFF`|`0x01E8_0000` to `0x01EF_FFFF`|512&nbsp;KiB|ARC CPU cluster shared memory (CSM)<br/>`0x1000_0000` on the [ARC XBAR](#arc-xbar-4-gib)|
|`0x1FF0_0000` to `0x1FFF_FFFF`|`0x01F0_0000` to `0x01FF_FFFF`|1 MiB|Mapped to [APB peripherals](#apb-peripherals-1-mib)<br/>`0x8000_0000` on the [ARC XBAR](#arc-xbar-4-gib)|

### NoC to ARC tile (64 GiB)

|Address range (from NoC)|Size|Contents|
|---|--:|---|
|`0x0_0000_0000` to `0x7_FFFF_FFFF`|32 GiB|Routed to [DRAM D0 tiles](../DRAMTile/README.md) (identity address mapping)|
|`0x8_0000_0000` to `0x8_FFFF_FFFF`|4 GiB|Mapped to [ARC XBAR](#arc-xbar-4-gib)|
|`0x9_0000_0000` to `0xE_FFFF_FFFF`|24 GiB|Reserved|
|`0xF_0000_0000` to `0xF_FFFF_FFFF`|4 GiB|[NIU configuration / status](../NoC/MemoryMap.md)|

### ARC CPU (4 GiB)

|Address range (ARC CPU)|Size|Contents|
|---|--:|---|
|`0x0000_0000` to `0x0000_0FFF`|4 KiB|Core-local instruction RAM (ICCM)|
|`0x0000_1000` to `0x0FFF_FFFF`|255.99 MiB|Routed to [ARC XBAR](#arc-xbar-4-gib) (identity address mapping)|
|`0x1000_0000` to `0x1007_FFFF`|512 KiB|ARC CPU cluster shared memory (CSM)|
|`0x1008_0000` to `0xFFFF_FFFF`|3.74 GiB|Routed to [ARC XBAR](#arc-xbar-4-gib) (identity address mapping)|

### ARC XBAR (4 GiB)

|Address range (ARC XBAR)|Size|Contents|
|---|--:|---|
|`0x0000_0000` to `0x0FFF_FFFF`|256 MiB|Reserved|
|`0x1000_0000` to `0x1007_FFFF`|512 KiB|ARC CPU cluster shared memory (CSM)|
|`0x1008_0000` to `0x7FFF_FFFF`|1.74 GiB|Reserved|
|`0x8000_0000` to `0x800F_FFFF`|1 MiB|Mapped to [APB peripherals](#apb-peripherals-1-mib)|
|`0x8010_0000` to `0x8FFF_FFFF`|255 MiB|Reserved|
|`0x9000_0000` to `0x9FFF_FFFF`|4x 64 MiB|TLB windows to DRAM D0|
|`0xA000_0000` to `0xAFFF_FFFF`|4x 64 MiB|TLB windows to PCI Express|
|`0xB000_0000` to `0xBFFF_FFFF`|256 MiB|Reserved|
|`0xC000_0000` to `0xCFFF_FFFF`|16x 16 MiB|TLB windows to NoC #0|
|`0xD000_0000` to `0xDFFF_FFFF`|16x 16 MiB|TLB windows to NoC #1|
|`0xE000_0000` to `0xFFFF_FFFF`|512 MiB|Reserved|

### APB peripherals (1 MiB)

The peripherals in this range exist so that firmware on the ARC CPU can configure them. Customer software is not expected to access most of the things in this range, and risks damaging the hardware if it does.

|Address range (APB Peripherals)|Size|Contents|Customer access|
|---|--:|---|---|
|`0x0_0000` to `0x0_FFFF`|64 KiB|ROM|No, risks damaging hardware|
|`0x1_0000` to `0x1_FFFF`|64 KiB|JTAG|No, risks damaging hardware|
|`0x2_0000` to `0x2_FFFF`|64 KiB|PLLs|No, risks damaging hardware|
|`0x3_0000` to `0x3_FFFF`|64 KiB|[Reset Unit](ResetUnit.md)|Where documented|
|`0x4_0000` to `0x4_FFFF`|64 KiB|eFuses|No, risks damaging hardware|
|`0x5_0000` to `0x5_0FFF`|4 KiB|[NIU #0 configuration / status](../NoC/MemoryMap.md)|Yes, but read-only|
|`0x5_1000` to `0x5_107F`|128 B|TLB window configuration for NIU #0|Yes, but read-only and undocumented|
|`0x5_1080` to `0x5_7FFF`|27.9 KiB|Reserved|N/A|
|`0x5_8000` to `0x5_8FFF`|4 KiB|[NIU #1 configuration / status](../NoC/MemoryMap.md)|Yes, but read-only|
|`0x5_9000` to `0x5_907F`|128 B|TLB window configuration for NIU #1|Yes, but read-only and undocumented|
|`0x5_9080` to `0x5_FFFF`|27.9 KiB|Reserved|N/A|
|`0x6_0000` to `0x6_FFFF`|64 KiB|I2C|No, risks damaging hardware|
|`0x7_0000` to `0x7_FFFF`|64 KiB|SPI|No, risks damaging hardware|
|`0x8_0000` to `0xF_FFFF`|512 KiB|Reserved|N/A|
