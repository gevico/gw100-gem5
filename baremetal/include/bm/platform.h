/**
 * @file  platform.h
 * @brief 裸机测试所在平台（gem5 VExpress_GEM5_V2）的地址常量
 *
 * 只收录测试会用到的地址，全部取自 gem5/src/dev/arm/RealView.py 中
 * VExpress_GEM5_Base（内存映射、PCI host）与 VExpress_GEM5_V2_Base
 * （GICv3、ITS）的定义。换平台时只改这里。
 *
 * 裸机程序在 MMU 关闭的状态下运行：所有数据访问按 Device 内存处理，
 * 必须自然对齐，也没有 cache。
 */

#ifndef BM_PLATFORM_H
#define BM_PLATFORM_H

/* host DRAM：程序从这里加载运行，见 linker.ld */
#define BM_DRAM_BASE            0x80000000UL
/*
 * 设备 DMA 测试用的 host 缓冲：程序之后的一块固定区域，16 MiB，放队列、
 * SGL 和数据缓冲。HDMA 的测试固件按这个约定的地址访问 host 内存
 * （firmware/fw/hdma_test.c 的 HOST_BUF）
 */
#define BM_DMA_BUF_BASE         0x90000000UL

/*
 * GIC 与 ITS 的内存表（LPI 配置表与 pending 表、ITS 的设备表、集合表、
 * 命令队列和 ITT）：DMA 缓冲之后的 1 MiB，由 bm_its_init 按需清零
 */
#define BM_GIC_MEM_BASE         0x91000000UL
#define BM_GIC_MEM_SIZE         0x00100000UL

/*
 * PCIe 配置空间（ECAM）：bus/dev/fn 编码为
 * (bus << 20) | (dev << 15) | (fn << 12)
 */
#define BM_PCI_ECAM_BASE        0x30000000UL
#define BM_PCI_ECAM_SIZE        0x10000000UL

/*
 * PCIe 内存空间：BAR 中写入的是总线地址，CPU 访问地址 = 本基址 + 总线地址
 * （GenericPciHost 的 pci_mem_base）。平台只有一个窗口：总线地址
 * 0 – 0x3FFF_FFFF 对应 CPU 地址 0x4000_0000 – 0x7FFF_FFFF（1 GiB），
 * 没有 4 GiB 以上的窗口，64 位 BAR 也在其中分配。
 */
#define BM_PCI_MEM_CPU_BASE     0x40000000UL

/* 分配给 32 位 BAR 的总线地址范围（避开总线地址 0） */
#define BM_PCI_MEM32_BUS_BASE   0x00100000UL
#define BM_PCI_MEM32_BUS_END    0x20000000UL

/* 分配给 64 位 BAR 的总线地址范围：同一窗口的后一半 */
#define BM_PCI_MEM64_BUS_BASE   0x20000000UL
#define BM_PCI_MEM64_BUS_END    0x40000000UL

/* GICv3 的 distributor、redistributor 与 ITS（GITS_TRANSLATER 在 +0x10040） */
#define BM_GICD_BASE            0x2C000000UL
#define BM_GICR_BASE            0x2C010000UL
#define BM_GITS_BASE            0x2E010000UL

#endif /* BM_PLATFORM_H */
