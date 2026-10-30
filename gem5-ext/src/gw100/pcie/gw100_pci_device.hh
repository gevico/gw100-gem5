/**
 * @file  gw100_pci_device.hh
 * @brief GW100 的极简 PCIe endpoint
 *
 * BAR0 寄存器：
 *   0x00 VERSION   RO，当前读回 1
 *   0x04 STATUS    RO，当前为 0
 *   0x08 CMD_ADDR  RW，64 位
 *   0x10 DOORBELL  RW，32 位
 *
 * DMA、命令/完成记录和中断后续接入。
 *
 * @see spec-pcie, spec-hdi
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
