/**
 * @file  pcie_capability.hh
 * @brief PCI Express capability
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_CONFIG_CAPABILITY_PCIE_CAPABILITY_HH__
#define __GW100_PCIE_CONFIG_CAPABILITY_PCIE_CAPABILITY_HH__

#include <cstdint>

#include "src/gw100/pcie/config/capability/capability.hh"

namespace gem5
{
namespace gw100
{

/** PCI Express capability：版本 2，Endpoint */
class PcieCapability : public Capability
{
  public:
    /** Device Control 相对 capability 起始的偏移 */
    static constexpr Addr DevCtl = 0x08;

    /** 构造；link_speed、link_width 写入 Link Status */
    PcieCapability(uint8_t link_speed, uint8_t link_width)
        : linkSpeed(link_speed), linkWidth(link_width)
    {}

    /** capability ID：PCI Express */
    uint8_t id() const override { return 0x10; }

    /** 写设备、链路的能力与控制寄存器的初值和可写位（spec-pcie §4.2） */
    void init(CfgBytes &cfg, CfgBytes &wmask, CfgBytes &w1c,
              Addr base) const override;

    /** 写了 Device Control 的 Initiate FLR（bit 15）时返回 Flr */
    CfgEvent afterWrite(const CfgBytes &old, CfgBytes &cfg, Addr base,
                        Addr off, unsigned size,
                        uint32_t val) const override;

  private:
    /** 当前链路速率：1 = Gen1 … 4 = Gen4 */
    uint8_t linkSpeed;
    /** 当前链路宽度 */
    uint8_t linkWidth;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_CONFIG_CAPABILITY_PCIE_CAPABILITY_HH__
