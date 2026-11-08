/**
 * @file  reset_shim.hh
 * @brief 设备完全复位期间，入站完成器代替功能逻辑响应 host 的规则
 *
 * 入站完成器所在的复位域不随设备完全复位复位（spec-clock-reset-power
 * §2.4）。从接受 BOOT_CTRL.DEV_RESET 到功能寄存器释放复位，它按 §4.3
 * 第 4 步的规则响应 BAR 访问：DEV_VERSION、DEV_CAPS 和 DEV_STATUS 的
 * 链路字段返回真实值，BOOT_STATUS 与 BAR0 的其他偏移返回 0，BAR2 读
 * 返回全 1，全部以成功完成结束、不记录错误；写入接受后丢弃。
 *
 * @see spec-clock-reset-power, spec-pcie
 */

#ifndef __GW100_PCIE_SHIM_RESET_SHIM_HH__
#define __GW100_PCIE_SHIM_RESET_SHIM_HH__

#include <cstdint>

#include "src/gw100/pcie/pcie_types.hh"

namespace gem5
{
namespace gw100
{

/** reset shim */
class ResetShim
{
  public:
    /**
     * 构造：version、dev_caps 为 DEV_VERSION、DEV_CAPS 的值，link_status
     * 为 DEV_STATUS 中链路速率和宽度字段的值，latency 为响应时间
     */
    ResetShim(uint32_t version, uint32_t dev_caps, uint32_t link_status,
              Tick latency);

    /** 是否处于 reset shim 状态 */
    bool active() const { return on; }

    /** 进入 reset shim（接受 DEV_RESET 时） */
    void enter() { on = true; }

    /** 退出 reset shim（功能寄存器释放复位时） */
    void exit() { on = false; }

    /** 按 shim 的规则完成对 BAR bar 的一次访问，返回响应时间 */
    Tick access(int bar, BarAccess &a) const;

  private:
    /** DEV_VERSION */
    const uint32_t version;
    /** DEV_CAPS */
    const uint32_t devCaps;
    /** DEV_STATUS 的链路速率和宽度字段 */
    const uint32_t linkStatus;
    /** 响应时间 */
    const Tick latency;
    /** 是否处于 reset shim 状态 */
    bool on = false;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_SHIM_RESET_SHIM_HH__
