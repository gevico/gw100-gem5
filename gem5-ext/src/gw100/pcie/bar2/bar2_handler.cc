/**
 * @file  bar2_handler.cc
 * @brief Bar2Handler 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/bar2/bar2_handler.hh"

#include <cstring>

namespace gem5
{
namespace gw100
{

/**
 * 处理一次访问：按窗口换算成 DPA，检查使能、目标范围和只读后经
 * DevmemBackend 访问设备内存
 */
Tick
Bar2Handler::access(BarAccess &a)
{
    const WindowTable::Result r = table.translate(a.off, a.size, a.write);
    if (r.fault == WindowTable::Fault::None) {
        devmem.access(r.target, a.size, a.write, a.data);
    } else {
        status.logError(r.fault == WindowTable::Fault::Disabled
                            ? StatusUnit::ErrWinDisabled
                            : StatusUnit::ErrWinTarget,
                        a.off, a.write, a.size, 2);
        if (!a.write)
            std::memset(a.data, 0xFF, a.size);
    }
    return latency;
}

} // namespace gw100
} // namespace gem5
