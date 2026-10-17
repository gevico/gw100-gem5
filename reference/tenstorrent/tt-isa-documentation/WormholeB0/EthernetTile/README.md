# Ethernet Tile

Each Ethernet tile contains:
  * [256 KiB of RAM called L1](L1.md)
  * [1x "Baby" RISCV (RV32IM) core](BabyRISCV/README.md)
  * [2x NoC connections](../NoC/README.md) allowing the local RISCV core to access data in other tiles, and allowing other tiles to access data from this tile
  * [1x NoC overlay](../NoC/Overlay/README.md) - a little coprocessor that can assist with NoC transactions
  * 1x 100 GbE Ethernet link, with Ethernet MAC / PCS / PHY, presented as [2x Ethernet TX queue](EthernetTxRx.md) and [2x Ethernet RX queue](EthernetTxRx.md)
