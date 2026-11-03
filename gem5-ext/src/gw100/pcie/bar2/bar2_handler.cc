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
 * 处理一次访问（spec-pcie §7.1）：启动窗口打开时交给 BootCtrl；
 * 调试窗口开启时按窗口换算成 DPA 访问设备内存；否则按关闭处理
 */
Tick
Bar2Handler::access(BarAccess &a)
{
    if (boot.windowOpen()) {
        // 启动窗口（§7.2）：只能写；读返回全 1，不报错
        if (a.write)
            boot.windowWrite(a.off, a.size, a.data);
        else
            std::memset(a.data, 0xFF, a.size);
        return latency;
    }
    if (!boot.debugWin) {
        // 关闭：读返回全 1，写被丢弃，记录 WIN_DISABLED
        status.logError(StatusUnit::ErrWinDisabled, a.off, a.write, a.size,
                        2);
        if (!a.write)
            std::memset(a.data, 0xFF, a.size);
        return latency;
    }
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
