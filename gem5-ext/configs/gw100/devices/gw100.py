"""
@file  gw100.py
@brief GW100 PCIe endpoint 的设备参数与创建

第一步只声明配置空间、BAR0 和 VERSION/STATUS/CMD_ADDR/DOORBELL
寄存器。这里只创建 SimObject，挂到哪条 PCIe 总线由板卡决定。

地址映射、BAR 窗口和 ABI 常量等单一真源重建后再从这里接入；
不要把驱动/测试用的地址硬编码到入口脚本里。

@see spec-pcie, spec-hdi
"""

from dataclasses import dataclass

from m5.objects import Gw100PciDevice
from m5.objects.PciDevice import PciMemBar


@dataclass(frozen=True)
class Gw100Config:
    """一组 GW100 设备参数。"""

    bar0_size: str = "4KiB"
    vendor_id: int = 0x1234
    device_id: int = 0x1000
    # 需要板卡提供的能力，入口脚本据此拒绝不兼容的组合
    requires: frozenset = frozenset({"pcie"})

    def make(self) -> Gw100PciDevice:
        """按本组参数创建一个 GW100 endpoint。"""
        return Gw100PciDevice(
            VendorID=self.vendor_id,
            DeviceID=self.device_id,
            BAR0=PciMemBar(size=self.bar0_size),
        )


GW100 = Gw100Config()
