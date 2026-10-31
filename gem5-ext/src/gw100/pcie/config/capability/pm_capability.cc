/**
 * @file  pm_capability.cc
 * @brief PmCapability 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/config/capability/pm_capability.hh"

namespace gem5
{
namespace gw100
{

/** 写 PMC、PMCSR 的初值；PMCSR 的 PowerState 可写 */
void
PmCapability::init(CfgBytes &cfg, CfgBytes &wmask, CfgBytes &w1c,
                   Addr base) const
{
    // 版本 3，只支持 D0/D3hot，No_Soft_Reset = 0（spec-pcie §4.2）
    put16(cfg, base + 0x02, 0x0003);
    put16(cfg, base + Pmcsr, 0x0000);
    wmask[base + Pmcsr] = 0x03;
}

/**
 * 忽略进入 D1、D2 的请求（恢复原来的 PowerState）；由 D3hot 回到 D0
 * 时返回 D3hotToD0（No_Soft_Reset = 0）
 */
CfgEvent
PmCapability::afterWrite(const CfgBytes &old, CfgBytes &cfg, Addr base,
                         Addr off, unsigned size, uint32_t val) const
{
    const Addr csr = base + Pmcsr;
    const uint8_t old_pm = old[csr] & 0x3;
    const uint8_t new_pm = cfg[csr] & 0x3;
    // 不支持 D1/D2：请求进入这两个状态时忽略写入
    if (new_pm == 1 || new_pm == 2)
        cfg[csr] = (cfg[csr] & ~0x3) | old_pm;
    if (old_pm == 3 && (cfg[csr] & 0x3) == 0)
        return CfgEvent::D3hotToD0;
    return CfgEvent::None;
}

} // namespace gw100
} // namespace gem5
