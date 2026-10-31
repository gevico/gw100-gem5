/**
 * @file  pm_capability.hh
 * @brief PCI Power Management capability
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_CONFIG_CAPABILITY_PM_CAPABILITY_HH__
#define __GW100_PCIE_CONFIG_CAPABILITY_PM_CAPABILITY_HH__

#include "src/gw100/pcie/config/capability/capability.hh"

namespace gem5
{
namespace gw100
{

/** PCI Power Management capability：版本 3，只支持 D0 和 D3hot */
class PmCapability : public Capability
{
  public:
    /** PMCSR 相对 capability 起始的偏移 */
    static constexpr Addr Pmcsr = 0x04;

    /** capability ID：PCI Power Management */
    uint8_t id() const override { return 0x01; }

    /** 写 PMC、PMCSR 的初值；PMCSR 的 PowerState 可写 */
    void init(CfgBytes &cfg, CfgBytes &wmask, CfgBytes &w1c,
              Addr base) const override;

    /**
     * 忽略进入 D1、D2 的请求（恢复原来的 PowerState）；由 D3hot 回到 D0
     * 时返回 D3hotToD0（No_Soft_Reset = 0）
     */
    CfgEvent afterWrite(const CfgBytes &old, CfgBytes &cfg, Addr base,
                        Addr off, unsigned size,
                        uint32_t val) const override;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_CONFIG_CAPABILITY_PM_CAPABILITY_HH__
