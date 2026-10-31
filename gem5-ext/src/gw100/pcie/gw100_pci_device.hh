/**
 * @file  gw100_pci_device.hh
 * @brief GW100 PCIe endpoint 的外壳
 *
 * 本类只负责与 gem5 的对接：PciEndpoint 的 BAR 与配置访问入口、
 * mem_port、功能级复位的编排和 checkpoint。各项行为由 Gw100PcieFactory
 * 组装的组件实现（见 pcie_factory.hh）：
 * - 配置空间 0x40 起：ConfigSpace 与各 Capability；
 * - BAR0：Bar0Handler 与 GlobalRegs、WindowRegs、BootRegs；
 * - BAR2：Bar2Handler、WindowTable、DevmemBackend；
 * - ERR/INT 状态：StatusUnit。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_GW100_PCI_DEVICE_HH__
#define __GW100_PCIE_GW100_PCI_DEVICE_HH__

#include <cstdint>
#include <memory>

#include "dev/pci/device.hh"
#include "params/Gw100PciDevice.hh"
#include "src/gw100/pcie/pcie_factory.hh"

namespace gem5
{
namespace gw100
{

/** GW100 的 PCIe endpoint：组件的外壳 */
class Gw100PciDevice : public PciEndpoint
{
  public:
    PARAMS(Gw100PciDevice);

    /** 按参数构造；全部组件由 Gw100PcieFactory 组装 */
    explicit Gw100PciDevice(const Params &p);

    /**
     * 返回名为 if_name 的端口：mem_port 是设备内存端口，其余交给
     * PciEndpoint
     */
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    /** 保存配置空间头部（基类）和各组件的状态 */
    void serialize(CheckpointOut &cp) const override;

    /** 从 checkpoint 恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp) override;

  protected:
    /** BAR 读访问的入口：交给该 BAR 的处理者 */
    Tick readDevice(PacketPtr pkt) override;

    /** BAR 写访问的入口：交给该 BAR 的处理者 */
    Tick writeDevice(PacketPtr pkt) override;

    /** 配置读：0x40 以下交给基类，0x40 起交给 ConfigSpace */
    Tick readConfig(PacketPtr pkt) override;

    /**
     * 配置写：0x40 以下交给基类；0x40 起交给 ConfigSpace，并按返回的
     * 事件处理 FLR 与 D3hot→D0
     */
    Tick writeConfig(PacketPtr pkt) override;

  private:
    /** 把一次 BAR 访问交给对应 BAR 的处理者，并完成响应 */
    Tick accessBar(PacketPtr pkt, bool is_write);

    /**
     * 功能级复位（FLR、D3hot→D0）：恢复配置空间和寄存器，清零
     * Command 和全部 BAR；设备内存内容保留
     */
    void functionReset(const char *why);

    /**
     * 构造一次配置写交给 PciEndpoint，用于复位配置空间头部（Command、
     * BAR），使 BAR 对象的状态一并更新
     */
    void cfgWriteCommon(Addr off, unsigned size, uint32_t val);

    /** 全部组件 */
    std::unique_ptr<PcieParts> parts;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_GW100_PCI_DEVICE_HH__
