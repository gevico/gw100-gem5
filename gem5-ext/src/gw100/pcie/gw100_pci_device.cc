/**
 * @file  gw100_pci_device.cc
 * @brief Gw100PciDevice 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/gw100_pci_device.hh"

#include <algorithm>

#include "base/trace.hh"
#include "debug/Gw100Pcie.hh"
#include "dev/pci/pcireg.h"
#include "mem/packet_access.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** Device Control 在配置空间中的偏移：PCI Express capability（0x50）+ 8 */
constexpr Addr DevCtl = 0x58;
/** HIF 事件位：BME_OFF（spec-mgmt-tile §6.3） */
constexpr uint32_t EvtBmeOff = 1u << 3;

} // anonymous namespace

/** 按参数构造；全部组件由 Gw100PcieFactory 组装 */
Gw100PciDevice::Gw100PciDevice(const Params &p)
    : PciEndpoint(p), parts(Gw100PcieFactory::build(p, *this)),
      link(p.mgmt_link),
      wdtFatalPin(name() + ".wdt_fatal", 0, this),
      wdtFatalEvent([this] { parts->bootCtrl.setDevFatal(); },
                    name() + ".wdtFatal"),
      devResetReqPin(name() + ".dev_reset_req", 0, this),
      funcResetPin(name() + ".func_reset")
{
    parts->hif.pciState = [this] { return pciState(); };
    parts->bootCtrl.onDevReset = [this] { devResetRequest(); };
    parts->bootCtrl.onFlrDone = [this] { parts->hdma.setHold(false); };
    funcResetPin.onChange([this](const bool &v) { funcReset(v); });
}

/** 检查端口，接入 ML 的 HIF 侧（接了管理链路时） */
void
Gw100PciDevice::init()
{
    // dma 端口由 HDMA 的 HostDmaPort 提供，DmaDevice 自带的 DmaPort
    // 不使用，所以跳过 DmaDevice::init() 对它的检查
    PioDevice::init();
    fatal_if(!parts->hostDma.isConnected(), "%s: dma 端口没有连接", name());
    parts->hostDma.toPhys = [this](Addr a) { return pciToDma(a); };
    updateHdmaPcie();
    if (!link)
        return;
    ml = &link->attach(Gw100MgmtLink::Side::Hif, *this, parts->hif);
    parts->hif.bindMl(ml);
    parts->bootCtrl.bindMl(ml);
    parts->hdmaMl.bind(ml);
    ml->onChange = [this] {
        // ML_HIF_ERR 非零经电平信号送到管理 Tile 侧（spec-mgmt-link
        // §8.2）；链路状态的变化交给启动与 FLR 控制和 HDMA
        ml->setHifErrLevel(ml->errReg() != 0);
        parts->bootCtrl.mlChanged();
        parts->hdma.wake();
    };
}

/** 撤销 ML HIF 侧的复位（接了管理链路时） */
void
Gw100PciDevice::startup()
{
    PciEndpoint::startup();
    if (link)
        link->release(Gw100MgmtLink::Side::Hif);
}

/** PCI_STATE（spec-pcie §6.5）：配置空间中固件需要的状态 */
uint32_t
Gw100PciDevice::pciState()
{
    uint16_t cmd = letoh(config().command);
    uint32_t v = 0;
    if (cmd & PCI_CMD_MSE)
        v |= 1u << 0;
    if (cmd & PCI_CMD_BME)
        v |= 1u << 1;
    if (cmd & 0x0400)  // Interrupt Disable
        v |= 1u << 5;
    return v;
}

/**
 * 返回名为 if_name 的端口：dma 是 HDMA 访问 host 内存的端口，
 * mem_port 是设备内存端口，wdt_fatal 是看门狗第二级超时的信号，
 * dev_reset_req、func_reset 是设备完全复位的请求与复位信号，其余
 * 交给 PciEndpoint
 */
Port &
Gw100PciDevice::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "dma")
        return parts->hostDma;
    if (if_name == "mem_port")
        return parts->devmem.port();
    if (if_name == "wdt_fatal")
        return wdtFatalPin;
    if (if_name == "dev_reset_req") {
        fatal_if(idx > 0, "%s: dev_reset_req 只有一个", name());
        return devResetReqPin;
    }
    if (if_name == "func_reset")
        return funcResetPin;
    return PciEndpoint::getPort(if_name, idx);
}

/** wdt_fatal 信号置位：经两级同步器后置位 DEV_FATAL */
void
Gw100PciDevice::raiseInterruptPin(int num)
{
    (void)num;
    if (!wdtFatalEvent.scheduled())
        schedule(wdtFatalEvent, clockEdge(Cycles(2)));
}

/** wdt_fatal 信号撤销：它保持到复位，不会撤销 */
void
Gw100PciDevice::lowerInterruptPin(int num)
{
    (void)num;
}

/** host 写 DEV_RESET：进入 reset shim，请求 CRC 复位 */
void
Gw100PciDevice::devResetRequest()
{
    if (parts->shim.active())
        return;  // 复位已在进行
    // 入站完成器的读响应时间固定，接受 DEV_RESET 时没有交给功能逻辑
    // 而尚未完成的读，复位排空立即结束（spec-clock-reset-power §4.3）
    parts->shim.enter();
    if (devResetReqPin.isConnected()) {
        devResetReqPin.raise();
        return;
    }
    // 不接管理 Tile 的配置：没有 CRC，直接复位功能逻辑
    funcReset(true);
    funcReset(false);
}

/**
 * CRC 的复位信号变化：asserted 时复位功能逻辑和 ML 的 HIF 侧，
 * 撤销时释放 ML 并退出 reset shim
 */
void
Gw100PciDevice::funcReset(bool asserted)
{
    if (asserted) {
        DPRINTF(Gw100Pcie, "设备完全复位：复位功能逻辑\n");
        parts->devReset();
        if (wdtFatalEvent.scheduled())
            deschedule(wdtFatalEvent);
        if (link)
            link->reset(Gw100MgmtLink::Side::Hif);
        if (devResetReqPin.isConnected())
            devResetReqPin.lower();
        return;
    }
    DPRINTF(Gw100Pcie, "设备完全复位：释放功能逻辑，退出 reset shim\n");
    if (link)
        link->release(Gw100MgmtLink::Side::Hif);
    parts->shim.exit();
}

/** 把一次 BAR 访问交给对应 BAR 的处理者，并完成响应 */
Tick
Gw100PciDevice::accessBar(PacketPtr pkt, bool is_write)
{
    int bar;
    Addr off;
    panic_if(!getBAR(pkt->getAddr(), bar, off),
             "%s: 访问地址 %#x 不在任何 BAR 内", name(), pkt->getAddr());
    BarHandler *h = parts->bars[bar];
    panic_if(!h, "%s: BAR%d 没有处理者", name(), bar);
    BarAccess a{off, unsigned(pkt->getSize()), is_write,
                pkt->getPtr<uint8_t>()};
    const Tick lat =
        parts->shim.active() ? parts->shim.access(bar, a) : h->access(a);
    pkt->makeAtomicResponse();
    return lat;
}

/** BAR 读访问的入口：交给该 BAR 的处理者 */
Tick
Gw100PciDevice::readDevice(PacketPtr pkt)
{
    return accessBar(pkt, false);
}

/** BAR 写访问的入口：交给该 BAR 的处理者 */
Tick
Gw100PciDevice::writeDevice(PacketPtr pkt)
{
    return accessBar(pkt, true);
}

/** 配置读：0x40 以下交给基类，0x40 起交给 ConfigSpace */
Tick
Gw100PciDevice::readConfig(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() & (CfgSpaceSize - 1);
    if (off < ConfigSpace::OwnBase)
        return PciDevice::readConfig(pkt);

    const unsigned size = pkt->getSize();
    panic_if(off + size > CfgSpaceSize, "%s: 配置空间访问越界", name());
    const uint32_t val = parts->config.read(off, size);
    switch (size) {
      case 1: pkt->setLE<uint8_t>(uint8_t(val)); break;
      case 2: pkt->setLE<uint16_t>(uint16_t(val)); break;
      case 4: pkt->setLE<uint32_t>(val); break;
      default: panic("%s: 非法的配置空间访问宽度 %u", name(), size);
    }
    pkt->makeAtomicResponse();
    return configDelay;
}

/**
 * 配置写：0x40 以下交给基类；0x40 起交给 ConfigSpace，并按返回的
 * 事件处理 FLR 与 D3hot→D0
 */
Tick
Gw100PciDevice::writeConfig(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() & (CfgSpaceSize - 1);
    if (off < ConfigSpace::OwnBase) {
        Tick t = PciEndpoint::writeConfig(pkt);
        updateBusMaster();
        return t;
    }

    const unsigned size = pkt->getSize();
    panic_if(off + size > CfgSpaceSize, "%s: 配置空间访问越界", name());
    uint32_t val;
    switch (size) {
      case 1: val = pkt->getLE<uint8_t>(); break;
      case 2: val = pkt->getLE<uint16_t>(); break;
      case 4: val = pkt->getLE<uint32_t>(); break;
      default: panic("%s: 非法的配置空间访问宽度 %u", name(), size);
    }

    const CfgEvent ev = parts->config.write(off, size, val);
    pkt->makeAtomicResponse();
    updateHdmaPcie();
    if (ev == CfgEvent::Flr)
        functionReset("FLR");
    else if (ev == CfgEvent::D3hotToD0)
        functionReset("D3hot→D0");  // No_Soft_Reset = 0
    return configDelay;
}

/**
 * 构造一次配置写交给 PciEndpoint，用于复位配置空间头部（Command、
 * BAR），使 BAR 对象的状态一并更新
 */
void
Gw100PciDevice::cfgWriteCommon(Addr off, unsigned size, uint32_t val)
{
    Addr addr = upstreamInterface.configRange().start() + off;
    RequestPtr req =
        std::make_shared<Request>(addr, size, 0, parts->devmem.id());
    Packet pkt(req, MemCmd::WriteReq);
    uint8_t buf[4] = {};
    pkt.dataStatic(buf);
    if (size == 2)
        pkt.setLE<uint16_t>(uint16_t(val));
    else
        pkt.setLE<uint32_t>(val);
    PciEndpoint::writeConfig(&pkt);
}

/**
 * 功能级复位（FLR、D3hot→D0）：恢复配置空间和寄存器，清零
 * Command 和全部 BAR；设备内存内容保留
 */
void
Gw100PciDevice::functionReset(const char *why)
{
    DPRINTF(Gw100Pcie, "%s：复位配置空间、BAR 和寄存器\n", why);
    parts->config.reset();
    parts->resetRegisters();
    cfgWriteCommon(PCI_COMMAND, 2, 0);
    for (int i = 0; i < 6; i++)
        cfgWriteCommon(PCI0_BASE_ADDR0 + 4 * i, 4, 0);
    // FLR 的门控、排空与通知固件（spec-pcie §13.2）；HDMA 已随寄存器
    // 复位，保持复位直到固件确认（spec-host-dma §8.4）
    parts->hdma.setHold(true);
    lastBme = false;
    parts->hdma.setBusMaster(false);
    updateHdmaPcie();
    parts->bootCtrl.flr();
}

/** 按 Device Control 的 MPS、MRRS、Extended Tag 更新 HDMA */
void
Gw100PciDevice::updateHdmaPcie()
{
    uint32_t dc = parts->config.read(DevCtl, 2);
    HdmaEngine::PcieParams pp;
    pp.mps = 128u << ((dc >> 5) & 7);
    pp.mrrs = std::min(128u << ((dc >> 12) & 7),
                       unsigned(params().hdma_max_read_request));
    pp.extTag = (dc >> 8) & 1;
    parts->hdma.setPcie(pp);
}

/** Bus Master Enable 变化：通知 HDMA；由 1 变 0 时发出 BME_OFF 事件 */
void
Gw100PciDevice::updateBusMaster()
{
    bool bme = letoh(config().command) & PCI_CMD_BME;
    if (bme == lastBme)
        return;
    lastBme = bme;
    parts->hdma.setBusMaster(bme);
    if (!bme)
        parts->bootCtrl.hostEvent(EvtBmeOff);
}

/** 保存配置空间头部（基类）和各组件的状态 */
void
Gw100PciDevice::serialize(CheckpointOut &cp) const
{
    PciEndpoint::serialize(cp);
    parts->serialize(cp);
}

/** 从 checkpoint 恢复 serialize() 保存的状态 */
void
Gw100PciDevice::unserialize(CheckpointIn &cp)
{
    PciEndpoint::unserialize(cp);
    parts->unserialize(cp);
}

} // namespace gw100
} // namespace gem5
