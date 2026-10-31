/**
 * @file  pcie_factory.hh
 * @brief 按参数组装 GW100 PCIe 的各组件
 *
 * PcieParts 持有全部组件；Gw100PcieFactory 检查参数、创建组件、登记
 * capability 和寄存器块，并把组件互相连接。新增组件（MSI-X 表、HDI
 * 寄存器、doorbell 页等）只需在这里创建和登记。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_PCIE_FACTORY_HH__
#define __GW100_PCIE_PCIE_FACTORY_HH__

#include <array>
#include <memory>
#include <string>

#include "params/Gw100PciDevice.hh"
#include "sim/serialize.hh"
#include "sim/sim_object.hh"
#include "src/gw100/pcie/bar0/bar0_handler.hh"
#include "src/gw100/pcie/bar0/boot_regs.hh"
#include "src/gw100/pcie/bar0/global_regs.hh"
#include "src/gw100/pcie/bar0/window_regs.hh"
#include "src/gw100/pcie/bar2/bar2_handler.hh"
#include "src/gw100/pcie/bar2/devmem_backend.hh"
#include "src/gw100/pcie/config/config_space.hh"
#include "src/gw100/pcie/status/status_unit.hh"
#include "src/gw100/pcie/window/window_table.hh"

namespace gem5
{
namespace gw100
{

/** GW100 PCIe 的全部组件 */
struct PcieParts
{
    /**
     * 按参数构造各组件；name 是所属设备的名字，devmem_id 是访问设备
     * 内存的请求者 ID。组件之间的登记与连接由 Gw100PcieFactory 完成
     */
    PcieParts(const Gw100PciDeviceParams &p, const std::string &name,
              RequestorID devmem_id);

    /**
     * 功能级复位：把寄存器和窗口恢复为复位值；配置空间由外壳另行复位
     */
    void resetRegisters();

    /** 按组件分节保存全部状态 */
    void serialize(CheckpointOut &cp) const;

    /** 按组件分节恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp);

    /** ERR_* 与 INT_* 的状态 */
    StatusUnit status;
    /** 配置空间 0x40 起的部分 */
    ConfigSpace config;
    /** BAR2 窗口的配置与地址转换 */
    WindowTable windows;
    /** 访问设备内存的后端 */
    DevmemBackend devmem;
    /** BAR0 的全局寄存器块 */
    GlobalRegs global;
    /** BAR0 的窗口配置寄存器块 */
    WindowRegs windowRegs;
    /** BAR0 的启动寄存器块 */
    BootRegs boot;
    /** BAR0 的处理者 */
    Bar0Handler bar0;
    /** BAR2 的处理者 */
    Bar2Handler bar2;
    /** 按 BAR 号索引的处理者；没有处理者的 BAR 为 nullptr */
    std::array<BarHandler *, 6> bars{};
};

/** 组装 GW100 PCIe 的各组件 */
class Gw100PcieFactory
{
  public:
    /**
     * 检查参数，创建全部组件，登记 capability 与寄存器块并互相连接；
     * owner 是所属设备，用于取名字和注册请求者 ID
     */
    static std::unique_ptr<PcieParts> build(const Gw100PciDeviceParams &p,
                                            SimObject &owner);
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_PCIE_FACTORY_HH__
