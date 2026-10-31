/**
 * @file  platform.h
 * @brief 裸机测试所在平台（gem5 VExpress_GEM5_Foundation）的地址常量
 *
 * 只收录测试会用到的地址，全部取自 gem5/src/dev/arm/RealView.py 中
 * VExpress_GEM5_Foundation 的定义。换平台时只改这里。
 *
 * 裸机程序在 MMU 关闭的状态下运行：所有数据访问按 Device 内存处理，
 * 必须自然对齐，也没有 cache。
 */

#ifndef BM_PLATFORM_H
#define BM_PLATFORM_H

/* host DRAM：程序从这里加载运行，见 linker.ld */
#define BM_DRAM_BASE            0x80000000UL

/*
 * PCIe 配置空间（ECAM）：bus/dev/fn 编码为
 * (bus << 20) | (dev << 15) | (fn << 12)
 */
#define BM_PCI_ECAM_BASE        0x40000000UL
#define BM_PCI_ECAM_SIZE        0x10000000UL

/*
 * PCIe 内存空间：BAR 中写入的是总线地址，CPU 访问地址 = 本基址 + 总线地址
 * （GenericPciHost 的 pci_mem_base）。
 */
#define BM_PCI_MEM_CPU_BASE     0x4000000000UL

/* 分配给 32 位 BAR 的总线地址范围 */
#define BM_PCI_MEM32_BUS_BASE   0x10000000UL
#define BM_PCI_MEM32_BUS_END    0x80000000UL

/* 分配给 64 位 BAR 的总线地址范围 */
#define BM_PCI_MEM64_BUS_BASE   0x100000000UL
#define BM_PCI_MEM64_BUS_END    0x1000000000UL

/* GICv3：中断测试使用（阶段 2） */
#define BM_GICD_BASE            0x2F000000UL
#define BM_GICR_BASE            0x2F100000UL

#endif /* BM_PLATFORM_H */
