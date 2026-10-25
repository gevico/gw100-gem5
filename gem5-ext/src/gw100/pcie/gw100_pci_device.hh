/**
 * @file  gw100_pci_device.hh
 * @brief GW100 的极简 PCIe endpoint
 *
 * 只实现 BAR0 的 VERSION/STATUS 读和少量寄存器写；
 * DMA、命令/完成记录后续接入。
 *
 * @see SPEC-310, SPEC-400
 */

#ifndef __GW100_PCIE_GW100_PCI_DEVICE_HH__
#define __GW100_PCIE_GW100_PCI_DEVICE_HH__

#include <cstdint>

#include "dev/pci/device.hh"
#include "params/Gw100PciDevice.hh"

namespace gem5
{
namespace gw100
{

class Gw100PciDevice : public PciEndpoint
{
  public:
    PARAMS(Gw100PciDevice);
    explicit Gw100PciDevice(const Params &p);

    Tick readDevice(PacketPtr pkt) override;
    Tick writeDevice(PacketPtr pkt) override;

  private:
    static constexpr Addr Bar0Size = 0x1000;

    uint32_t version = 1;
    uint64_t cmdAddr = 0;
    uint32_t doorbell = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_GW100_PCI_DEVICE_HH__
