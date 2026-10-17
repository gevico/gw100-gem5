# `ethdump` sample

The [`ethdump.c`](ethdump.c) file is a self-contained application for listening on a single Blackhole Ethernet tile and writing all frames received by that tile to a pcap file on the host. An example of compiling and running it is:

```
$ gcc -O2 ethdump.c -o ethdump && ./ethdump --out=tt.pcap --generate-traffic --loopback-mode=2
^C
Captured 121 packets, wrote 18416 bytes to tt.pcap
```

The application can also print some information about the various Ethernet tiles on a card, for example:

```
$ gcc -O2 ethdump.c -o ethdump && ./ethdump --device=0 --hwinfo
|Tile|NoC #0  |Logical  |Port   |Training    |Serdes          |MAC Address      |
|----|--------|---------|-------|------------|----------------|-----------------|
|E0  |X=1 ,Y=1|X=20,Y=25|No     |Skipped     |N/A             |N/A              |
|E2  |X=2 ,Y=1|X=22,Y=25|No     |Skipped     |N/A             |N/A              |
|E4  |X=3 ,Y=1|X=24,Y=25|Up     |Ext Loopback|#2 lanes 0,1,2,3|20:8c:47:05:2b:c0|
|E6  |X=4 ,Y=1|X=25,Y=25|Down   |Timeout (AN)|#2 lanes 4,5,6,7|20:8c:47:05:2b:c1|
|E8  |X=5 ,Y=1|Harvested|N/A    |N/A         |N/A             |N/A              |
|E10 |X=6 ,Y=1|X=28,Y=25|Down   |Timeout (AN)|#4 lanes 4,5,6,7|20:8c:47:05:2b:c4|
|E12 |X=7 ,Y=1|X=30,Y=25|Down   |Timeout (AN)|#3 lanes 4,5,6,7|20:8c:47:05:2b:c6|
|E13 |X=10,Y=1|X=31,Y=25|Up     |Ext Loopback|#3 lanes 0,1,2,3|20:8c:47:05:2b:c7|
|E11 |X=11,Y=1|X=29,Y=25|Up     |Ext Loopback|#4 lanes 0,1,2,3|20:8c:47:05:2b:c5|
|E9  |X=12,Y=1|X=27,Y=25|Down   |Timeout (AN)|#5 lanes 0,1,2,3|20:8c:47:05:2b:c3|
|E7  |X=13,Y=1|X=26,Y=25|Up     |Ext Loopback|#5 lanes 4,5,6,7|20:8c:47:05:2b:c2|
|E5  |X=14,Y=1|Harvested|N/A    |N/A         |N/A             |N/A              |
|E3  |X=15,Y=1|X=23,Y=25|No     |Skipped     |N/A             |N/A              |
|E1  |X=16,Y=1|X=21,Y=25|No     |Skipped     |N/A             |N/A              |
```

## Usage notes

* Have multiple Tenstorrent devices? Use `--device=N` to choose which one gets used.
* Don't know which Ethernet tiles are which? `--hwinfo` will give you some information.
* Want to choose which Ethernet tile to record from? `--ethernet-x=X` is the answer (where `X` is either a [NoC #0 X coordinate](../../../NoC/Coordinates.md) or logical X coordinate).
* Don't have any other devices to connect to? Run with `--loopback-mode=2` to put the tile into loopback mode (and sometime later run with `--loopback-mode=0` to disable loopback mode). Then add `--generate-traffic` to ensure some packets are transmitted.
* Want to change the output file? `--output=FILENAME.pcap`.
* Not seeing any terminal output? No news is good news; output is only printed upon error or upon termination.
* Need to terminate the program? CTRL+C (or anything else to cause a SIGINT)
* Don't know what to do with a pcap file? Wireshark can view it.
* Want to vary the size of the receive rings? Try adding something like `--device-ring-size=64K --host-ring-size=4MB` (both must be powers of two).

## Implementation notes

Each Ethernet tile on Blackhole contains two RISCV cores: RISCV E0 and RISCV E1. E0 is likely running some Tenstorrent firmware, so `ethdump` exclusively uses E1.

Several pieces of memory are allocated on the device:
* On-device receive ring (typically 256 KiB)
* On-device metadata buffer (64 bytes)
* On-device RISCV machine code (~400 bytes)

Two major pieces of memory are allocated on the host and then pinned to make them visible to the device:
* Host receive ring (typically 2 MiB)
* Host metadata buffer (64 bytes)

The device's [Ethernet RX subsystem](../../EthernetTxRx.md) is configured to write all packets to the on-device receive ring. This ring is slightly awkward to work with, as:
* Its size is limited by the size of the Ethernet tile's L1. This is 512 KiB, but some of that L1 needs to be used to store RISCV machine code, so the largest possible power of two size is 256 KiB (the _RX subsystem_ doesn't require a power of two ring size, but requiring it makes `ethdump` simpler).
* The RX subsystem doesn't _directly_ say how much of the ring contains valid data; this needs to be inferred by computing `ETH_RXQ_BUF_PTR - ETH_RXQ_OUTSTANDING_WR_CNT * 96`, and this computation isn't monotonic (e.g. as a write of 64 bytes is started, `ETH_RXQ_BUF_PTR` increases by 64 and `ETH_RXQ_OUTSTANDING_WR_CNT` increases by 1, so the computed value _decreases_ by 32). As a workaround, the on-device code restores monotonicity by keeping track of a high watermark.
* If the ring _isn't_ configured to wrap, then we won't be able to receive more than the ring size in total. On the other hand, if the ring _is_ configured to wrap, the device might overwrite data before we've consumed it. As a workaround, the on-device code:
  * Enables wrap mode as the write pointer approaches the end of the ring.
  * Once the write pointer has wrapped, disables wrap mode and sets the write limit to just before the read pointer.
  * Once the read pointer has wrapped, restores the write limit to the full ring size.
  * Explicitly checks for drops caused by the ring being full.

In addition to the above workarounds, the main job of the on-device code is to shuttle data from the on-device receive ring to the host receive ring. This is done by instructing the [NIU](../../../NoC/MemoryMap.md) to copy bytes from the on-device receive ring (in the Ethernet tile's L1) to the PCIe tile, at which point the PCIe tile will send them onwards to the host, and they'll eventually appear in the host receive ring. The device also needs to inform the host of how much data has been written to the ring, which is the purpose of the metadata buffer: the device will store its write pointer to the on-device metadata buffer, then instruct an NIU to copy that buffer to the host, and then the host will load that write pointer from the host metadata buffer. There is some subtle memory ordering here:
1. The NIU needs to load from the on-device metadata buffer _after_ the on-device code has stored its write pointer to the on-device metadata buffer.
2. The metadata buffer needs to be received by the host _after_ the ring contents has been received by the host.

Problem 2 is solved via the `NOC_CMD_VC_STATIC` flag, which ensures that ordering is maintained all the way to the PCIe tile, at which point the usual PCIe ordering rules for (posted) writes take over and guarantee ordering for the remainder of the journey. Problem 1 _could_ be entirely solved with memory fences, but in the interest of saving a few cycles, the code merely makes reordering rare rather than impossible: this is fine though, as the metadata is pushed regularly, and the format is carefully designed to make occasional pushes of stale metadata benign.

The host informs the device of how much host ring it has consumed, with `ROUTER_CFG_4` being borrowed for this purpose. The on-device code uses this to ensure that it doesn't overwrite data in the host ring until the host has consumed that data.
