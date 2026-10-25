/**
 * @file  gw100_pci_device.cc
 * @brief Gw100PciDevice 的实现
 *
 * @see SPEC-310, SPEC-400
 */

#include "src/gw100/pcie/gw100_pci_device.hh"

#include "mem/packet_access.hh"

namespace gem5
{
namespace gw100
{

Gw100PciDevice::Gw100PciDevice(const Params &p) : PciEndpoint(p)
{
}

Tick
Gw100PciDevice::readDevice(PacketPtr pkt)
{
    int bar;
    Addr offset;
    if (!getBAR(pkt->getAddr(), bar, offset) || bar != 0 ||
        offset >= Bar0Size) {
        pkt->setBadAddress();
        pkt->makeAtomicResponse();
        return pioDelay;
    }

    uint64_t value = 0;
    switch (offset) {
      case 0x00:
        value = version;
        break;
      case 0x08:
        value = cmdAddr;
        break;
      case 0x10:
        value = doorbell;
        break;
      default:
        break;
    }

    if (pkt->getSize() == 4) {
        pkt->setLE<uint32_t>(static_cast<uint32_t>(value));
    } else if (pkt->getSize() == 8) {
        pkt->setLE<uint64_t>(value);
    } else {
        pkt->setBadAddress();
    }
    pkt->makeAtomicResponse();
    return pioDelay;
}

Tick
Gw100PciDevice::writeDevice(PacketPtr pkt)
{
    int bar;
    Addr offset;
    if (!getBAR(pkt->getAddr(), bar, offset) || bar != 0 ||
        offset >= Bar0Size) {
        pkt->setBadAddress();
        pkt->makeAtomicResponse();
        return pioDelay;
    }

    if (offset == 0x08 && pkt->getSize() == 8) {
        cmdAddr = pkt->getLE<uint64_t>();
    } else if (offset == 0x10 && pkt->getSize() == 4) {
        doorbell = pkt->getLE<uint32_t>();
    }

    pkt->makeAtomicResponse();
    return pioDelay;
}

} // namespace gw100
} // namespace gem5
