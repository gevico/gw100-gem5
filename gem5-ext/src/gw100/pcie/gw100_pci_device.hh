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
 * - ERR/INT 状态：StatusUnit；
 * - Host DMA：HdmaEngine，经 dma 端口访问 host 内存，经 mem_port 访问
 *   设备内存，寄存器经 HIF 区，BAR0 0x6000 是它的只读镜像；
 * - 中断：IntCtrl，MSI-X 消息与 DMA 写共用 dma 端口，INTA 经
 *   PciEndpoint 的 intrPost/intrClear。
 * 外壳还接收看门狗第二级超时的专用信号 wdt_fatal（spec-mgmt-tile
 * §7.4），经两级同步器后置位 DEV_FATAL；host 写 DEV_RESET 时进入
 * reset shim 并经 dev_reset_req 请求 CRC 复位，由 CRC 经 func_reset
 * 复位功能逻辑（spec-clock-reset-power §4.3）。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_GW100_PCI_DEVICE_HH__
#define __GW100_PCIE_GW100_PCI_DEVICE_HH__

#include <cstdint>
#include <memory>

#include "dev/intpin.hh"
#include "dev/pci/device.hh"
#include "sim/signal.hh"
#include "params/Gw100PciDevice.hh"
#include "src/gw100/ml/mgmt_link.hh"
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
     * 返回名为 if_name 的端口：dma 是 HDMA 访问 host 内存的端口，
     * mem_port 是设备内存端口，wdt_fatal 是看门狗第二级超时的信号，
     * dev_reset_req、func_reset 是设备完全复位的请求与复位信号，其余
     * 交给 PciEndpoint
     */
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    /** 检查端口，接入 ML 的 HIF 侧（接了管理链路时） */
    void init() override;

    /** 撤销 ML HIF 侧的复位（接了管理链路时） */
    void startup() override;

    /** 保存配置空间头部（基类）和各组件的状态 */
    void serialize(CheckpointOut &cp) const override;

    /** 从 checkpoint 恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp) override;

    /** wdt_fatal 信号置位：经两级同步器后置位 DEV_FATAL */
    void raiseInterruptPin(int num);

    /** wdt_fatal 信号撤销：它保持到复位，不会撤销 */
    void lowerInterruptPin(int num);

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

    /** PCI_STATE（spec-pcie §6.5）：配置空间中固件需要的状态 */
    uint32_t pciState();

    /** host 写 DEV_RESET：进入 reset shim，请求 CRC 复位 */
    void devResetRequest();

    /** 按 Device Control 的 MPS、MRRS、Extended Tag 更新 HDMA */
    void updateHdmaPcie();

    /**
     * Command 寄存器变化：Bus Master Enable 通知 HDMA 与中断控制器，由 1
     * 变 0 时发出 BME_OFF 事件；Interrupt Disable 通知中断控制器
     */
    void updateBusMaster();

    /** MSI-X capability 的 Enable、Function Mask 变化：通知中断控制器 */
    void updateMsix();

    /**
     * CRC 的复位信号变化：asserted 时复位功能逻辑和 ML 的 HIF 侧，
     * 撤销时释放 ML 并退出 reset shim
     */
    void funcReset(bool asserted);

    /** 全部组件 */
    std::unique_ptr<PcieParts> parts;
    /** 管理链路；为空时不接管理 Tile */
    Gw100MgmtLink *link;
    /** ML 的 HIF 侧适配器 */
    MlEndpoint *ml = nullptr;
    /** 看门狗第二级超时的专用信号（来自管理 Tile） */
    IntSinkPin<Gw100PciDevice> wdtFatalPin;
    /** 信号经两级同步器到达 clk_pcie 域时置位 DEV_FATAL 的事件 */
    EventFunctionWrapper wdtFatalEvent;
    /** 设备完全复位的请求，送给 CRC（管理 Tile） */
    IntSourcePin<Gw100PciDevice> devResetReqPin;
    /** CRC 送来的功能逻辑复位信号：true 为复位中 */
    SignalSinkPort<bool> funcResetPin;
    /** 上一次看到的 Bus Master Enable */
    bool lastBme = false;
    /** doorbell 在 ML HIF 侧的客户端编号 */
    unsigned dbClient = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_GW100_PCI_DEVICE_HH__
