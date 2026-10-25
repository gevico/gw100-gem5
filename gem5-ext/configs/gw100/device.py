"""
@file  device.py
@brief 创建并挂载 GW100 的 PCIe endpoint

第一步只声明配置空间、BAR0 和 VERSION/STATUS/CMD_ADDR/DOORBELL
寄存器；设备的 upstream/PCI 端口由 `RealView.attachPciDevice()`
统一接好。

@see SPEC-310, SPEC-400
"""

from m5.objects import Gw100PciDevice
from m5.objects.PciDevice import PciMemBar

from .params import Gw100Params


def attach_gw100(board, params: Gw100Params):
    """在 ARM board 的 PCIe 总线上挂一个 GW100 endpoint。"""
    dev = Gw100PciDevice(
        VendorID=params.vendor_id,
        DeviceID=params.device_id,
        BAR0=PciMemBar(size=params.bar0_size),
    )
    board._pci_devices.append(dev)
    board.realview.attachPciDevice(dev)
    return dev
