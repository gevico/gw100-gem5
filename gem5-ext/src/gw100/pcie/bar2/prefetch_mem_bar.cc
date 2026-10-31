/**
 * @file  prefetch_mem_bar.cc
 * @brief Gw100PrefetchMemBar 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/bar2/prefetch_mem_bar.hh"

namespace gem5
{
namespace gw100
{

/**
 * 写 BAR 寄存器：交给 PciMemBar 处理后在写回值中置 bit 3
 * （Prefetchable）；bit 3 是硬件固定位，不参与地址计算
 */
uint32_t
Gw100PrefetchMemBar::write(const PciUpstream::DeviceInterface &interface,
                           uint32_t val)
{
    return PciMemBar::write(interface, val & ~0x8u) | 0x8u;
}

} // namespace gw100
} // namespace gem5
