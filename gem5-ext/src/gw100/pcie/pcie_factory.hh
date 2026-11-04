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
#include <vector>

#include "params/Gw100PciDevice.hh"
#include "sim/serialize.hh"
#include "sim/clocked_object.hh"
#include "sim/sim_object.hh"
#include "src/gw100/pcie/bar0/bar0_handler.hh"
#include "src/gw100/pcie/bar0/boot_regs.hh"
#include "src/gw100/pcie/bar0/global_regs.hh"
#include "src/gw100/pcie/bar0/hdma_mirror.hh"
#include "src/gw100/pcie/bar0/window_regs.hh"
#include "src/gw100/pcie/bar2/bar2_handler.hh"
#include "src/gw100/pcie/bar2/devmem_backend.hh"
#include "src/gw100/pcie/boot/boot_ctrl.hh"
#include "src/gw100/pcie/config/config_space.hh"
#include "src/gw100/pcie/hdi/hdi_ctrl.hh"
#include "src/gw100/pcie/hdi/hdi_regs.hh"
#include "src/gw100/pcie/hdma/hdma_engine.hh"
#include "src/gw100/pcie/hdma/hdma_glue.hh"
#include "src/gw100/pcie/hif/hif_regs.hh"
#include "src/gw100/pcie/intr/int_ctrl.hh"
#include "src/gw100/pcie/intr/msix_regs.hh"
#include "src/gw100/pcie/shim/reset_shim.hh"
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
     * 内存的请求者 ID，em 用于调度定时事件。组件之间的登记与连接由
     * Gw100PcieFactory 完成
     */
    PcieParts(const Gw100PciDeviceParams &p, const std::string &name,
              RequestorID devmem_id, RequestorID dma_id,
              ClockedObject &owner);

    /**
     * 功能级复位：把寄存器和窗口恢复为复位值；配置空间由外壳另行复位
     */
    void resetRegisters();

    /**
     * 设备完全复位：在 resetRegisters() 之外，再复位 FLR 保留的启动
     * 状态、FW_VERSION 和 DEV_FATAL；ML 由外壳另行复位
     */
    void devReset();

    /** 按组件分节保存全部状态 */
    void serialize(CheckpointOut &cp) const;

    /** 按组件分节恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp);

    /** ERR_* 与 INT_* 的状态 */
    StatusUnit status;
    /** 启动与 FLR 控制（BOOT_STATUS、启动窗口、FLR 门控） */
    BootCtrl bootCtrl;
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
    /** 固件可见的 HIF 寄存器区（ML 的 HIF 侧目标） */
    HifRegs hif;
    /** BAR0 的处理者 */
    Bar0Handler bar0;
    /** BAR2 的处理者 */
    Bar2Handler bar2;
    /** 设备完全复位期间响应 BAR 访问的 reset shim */
    ResetShim shim;
    /** HDMA 的时钟（clk_pcie） */
    HdmaClock hdmaClock;
    /** HDMA 访问 host 内存的 dma 端口 */
    HostDmaPort hostDma;
    /** HDMA 在 ML HIF 侧的客户端 */
    MlHdmaClient hdmaMl;
    /** Host DMA 引擎 */
    HdmaEngine hdma;
    /** BAR0 的 HDMA 寄存器只读镜像 */
    HdmaMirror hdmaMirror;
    /** 中断控制器：MSI-X 与 INTx */
    IntCtrl intCtrl;
    /** BAR0 的 MSI-X 表 */
    MsixTable msixTable;
    /** BAR0 的 MSI-X PBA */
    MsixPba msixPba;
    /** HDI 控制器在 HIF 中的状态 */
    HdiCtrl hdi;
    /** BAR0 的 HDI 控制器寄存器块 */
    HdiRegsBlock hdiRegs;
    /** BAR0 的 doorbell 页：管理队列与各上下文 */
    std::vector<std::unique_ptr<DoorbellPage>> doorbells;
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
                                            ClockedObject &owner);
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_PCIE_FACTORY_HH__
