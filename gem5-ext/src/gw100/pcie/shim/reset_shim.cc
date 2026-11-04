/**
 * @file  reset_shim.cc
 * @brief ResetShim 的实现
 *
 * @see spec-clock-reset-power, spec-pcie
 */

#include "src/gw100/pcie/shim/reset_shim.hh"

#include <cstring>

namespace gem5
{
namespace gw100
{

/**
 * 构造：version、dev_caps 为 DEV_VERSION、DEV_CAPS 的值，link_status
 * 为 DEV_STATUS 中链路速率和宽度字段的值，latency 为响应时间
 */
ResetShim::ResetShim(uint32_t version, uint32_t dev_caps,
                     uint32_t link_status, Tick latency)
    : version(version), devCaps(dev_caps), linkStatus(link_status),
      latency(latency)
{
}

/** 按 shim 的规则完成对 BAR bar 的一次访问，返回响应时间 */
Tick
ResetShim::access(int bar, BarAccess &a) const
{
    if (a.write)
        return latency;  // 接受后丢弃，没有副作用
    if (bar != 0) {
        std::memset(a.data, 0xFF, a.size);
        return latency;
    }
    // BAR0：逐字节取 DEV_VERSION、DEV_CAPS、DEV_STATUS 的真实值，其余 0
    for (unsigned i = 0; i < a.size; i++) {
        Addr b = a.off + i;
        uint32_t w = 0;
        if (b / 4 == 0)
            w = version;
        else if (b / 4 == 1)
            w = devCaps;
        else if (b / 4 == 2)
            w = linkStatus;
        a.data[i] = uint8_t(w >> (8 * (b % 4)));
    }
    return latency;
}

} // namespace gw100
} // namespace gem5
