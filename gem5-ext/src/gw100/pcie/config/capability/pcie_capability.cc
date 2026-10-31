/**
 * @file  pcie_capability.cc
 * @brief PcieCapability 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/config/capability/pcie_capability.hh"

namespace gem5
{
namespace gw100
{

/** 写设备、链路的能力与控制寄存器的初值和可写位（spec-pcie §4.2） */
void
PcieCapability::init(CfgBytes &cfg, CfgBytes &wmask, CfgBytes &w1c,
                     Addr base) const
{
    put16(cfg, base + 0x02, 0x0002);
    // Device Capabilities：MPS Supported = 256 B，Extended Tag，FLR Capable
    put32(cfg, base + 0x04, 0x1u | (1u << 5) | (1u << 28));
    // Device Control：Enable Relaxed Ordering、Extended Tag Enable、
    // Enable No Snoop 默认为 1，MPS = 128 B，MRRS = 512 B
    put16(cfg, base + DevCtl,
          (1u << 4) | (1u << 8) | (1u << 11) | (2u << 12));
    put16(wmask, base + DevCtl, 0x79FF);
    // Device Status：错误检测位写 1 清零
    put16(w1c, base + 0x0A, 0x000F);
    // Link Capabilities：最高 Gen4（16 GT/s）、x4、只支持 ASPM L1
    put32(cfg, base + 0x0C, 0x4u | (4u << 4) | (2u << 10) | (6u << 15));
    // Link Control：ASPM 控制、Common Clock、Extended Synch 可写
    put16(wmask, base + 0x10, 0x00C3);
    // Link Status：当前速率与宽度，Slot Clock Configuration
    put16(cfg, base + 0x12,
          (linkSpeed & 0xF) | ((linkWidth & 0x3F) << 4) | (1u << 12));
    // Device Capabilities 2：完成超时范围 A、B，不支持关闭完成超时
    put32(cfg, base + 0x24, 0x3);
    // Device Control 2：完成超时值可写
    put16(wmask, base + 0x28, 0x000F);
    // Link Capabilities 2：支持 2.5/5/8/16 GT/s
    put32(cfg, base + 0x2C, 0x1E);
    // Link Control 2：Target Link Speed 默认 Gen4，可写
    put16(cfg, base + 0x30, 0x0004);
    put16(wmask, base + 0x30, 0x000F);
}

/** 写了 Device Control 的 Initiate FLR（bit 15）时返回 Flr */
CfgEvent
PcieCapability::afterWrite(const CfgBytes &old, CfgBytes &cfg, Addr base,
                           Addr off, unsigned size, uint32_t val) const
{
    // Initiate FLR 位于 Device Control 高字节的 bit 7；它不在可写位中，
    // 写入后读回仍为 0
    const Addr flr_byte = base + DevCtl + 1;
    if (off <= flr_byte && flr_byte < off + size &&
        ((val >> (8 * (flr_byte - off))) & 0x80))
        return CfgEvent::Flr;
    return CfgEvent::None;
}

} // namespace gw100
} // namespace gem5
