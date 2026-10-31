/**
 * @file  gw100_pci_device.cc
 * @brief Gw100PciDevice 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/gw100_pci_device.hh"

#include "base/trace.hh"
#include "debug/Gw100Pcie.hh"
#include "dev/pci/pcireg.h"
#include "mem/packet_access.hh"

namespace gem5
{
namespace gw100
{

/** 按参数构造；全部组件由 Gw100PcieFactory 组装 */
Gw100PciDevice::Gw100PciDevice(const Params &p)
    : PciEndpoint(p), parts(Gw100PcieFactory::build(p, *this))
{}

/**
 * 返回名为 if_name 的端口：mem_port 是设备内存端口，其余交给
 * PciEndpoint
 */
Port &
Gw100PciDevice::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "mem_port")
        return parts->devmem.port();
    return PciEndpoint::getPort(if_name, idx);
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
    const Tick lat = h->access(a);
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
    if (off < ConfigSpace::OwnBase)
        return PciEndpoint::writeConfig(pkt);

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
