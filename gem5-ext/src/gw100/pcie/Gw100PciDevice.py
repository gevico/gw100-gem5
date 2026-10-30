"""
@file  Gw100PciDevice.py
@brief GW100 的极简 PCIe endpoint 声明

第一步只暴露 PCI 配置空间、BAR0 和 VERSION/STATUS 寄存器；
命令记录、完成记录和 DMA 后续接入。

@see spec-pcie, spec-hdi
"""

from m5.objects.PciDevice import PciEndpoint, PciMemBar
from m5.params import *


class Gw100PciDevice(PciEndpoint):
    """GW100 的最小 PCIe endpoint，用于验证枚举和 BAR 访问。"""

    type = "Gw100PciDevice"
    cxx_header = "src/gw100/pcie/gw100_pci_device.hh"
    cxx_class = "gem5::gw100::Gw100PciDevice"

    VendorID = 0x1234
    DeviceID = 0x1000
    Revision = 0x01
    ClassCode = 0x12
    SubClassCode = 0x00
    ProgIF = 0x00
    InterruptPin = 0x00
    pci_dev = 0
    pci_func = 0

    BAR0 = PciMemBar(size="4KiB")
