/**
 * @file  prefetch_mem_bar.hh
 * @brief 可预取的内存 BAR
 *
 * gem5 的 PciMemBar 把 bit 3 当作地址位，不支持 Prefetchable 标志；
 * spec-pcie 要求 BAR2 可预取，以便 host 按写合并方式映射。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_BAR2_PREFETCH_MEM_BAR_HH__
#define __GW100_PCIE_BAR2_PREFETCH_MEM_BAR_HH__

#include <cstdint>

#include "dev/pci/device.hh"
#include "params/Gw100PrefetchMemBar.hh"

namespace gem5
{
namespace gw100
{

/** 可预取的内存 BAR：在 PciMemBar 的写回值中置 bit 3 */
class Gw100PrefetchMemBar : public PciMemBar
{
  public:
    /** 按参数构造；大小等检查由 PciMemBar 完成 */
    Gw100PrefetchMemBar(const Gw100PrefetchMemBarParams &p) : PciMemBar(p) {}

    /**
     * 写 BAR 寄存器：交给 PciMemBar 处理后在写回值中置 bit 3
     * （Prefetchable）；bit 3 是硬件固定位，不参与地址计算
     */
    uint32_t write(const PciUpstream::DeviceInterface &interface,
                   uint32_t val) override;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR2_PREFETCH_MEM_BAR_HH__
