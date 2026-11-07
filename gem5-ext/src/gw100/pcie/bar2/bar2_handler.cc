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

/** 通过 timing 后端处理一次访问，满时返回 false */
bool
Bar2Handler::accessAsync(BarAccess a, std::function<void(Tick)> done,
                         std::function<bool()> valid)
{
    if (boot.windowOpen() || !boot.debugWin ||
        table.translate(a.off, a.size, a.write).fault !=
            WindowTable::Fault::None) {
        done(access(a));
        return true;
    }
    if (!devmem.canSend()) {
        return false;
    }
    auto target = table.translate(a.off, a.size, a.write).target;
    auto finish = [this, a, done, valid](bool ok) {
        if (!ok && valid()) {
            status.logError(StatusUnit::ErrWinTarget, a.off, a.write, a.size,
                            2);
            if (!a.write) {
                std::memset(a.data, 0xFF, a.size);
            }
        }
        done(latency);
    };
    if (a.write) {
        devmem.write(target, a.size, a.data, finish);
    } else {
        devmem.read(target, a.size, [a, finish](bool ok, const uint8_t *data) {
            if (ok) {
                std::memcpy(a.data, data, a.size);
            }
            finish(ok);
        });
    }
    return true;
}

} // namespace gw100
} // namespace gem5
