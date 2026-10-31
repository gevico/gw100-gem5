"""
@file  gw100.py
@brief GW100 PCIe endpoint 的设备参数与创建

参数按规格中的名字分组（pcie.*、devmem.*、addr.*、hdi.*、hdma.*），单一
真源重建后整体迁移过去。这里创建设备和它的设备内存，挂到哪条 PCIe 总线
由板卡决定。

设备内存暂用 SimpleMemory 代替 LPDDR5X：不在 host 的地址映射里，地址
可以与 host 重叠，BAR2 窗口经设备的 mem_port 访问它。

@see spec-pcie, spec-hdi
"""

from dataclasses import dataclass, field

from m5.objects import (
    AddrRange,
    Gw100PciDevice,
    Gw100PrefetchMemBar,
    SimpleMemory,
)
from m5.objects.PciDevice import PciMemBar, PciMemUpperBar
from m5.util.convert import toMemorySize


@dataclass(frozen=True)
class PcieParams:
    """pcie.* 参数（spec-pcie §14）。"""

    vendor_id: int = 0x1234     # 仿真用占位值，正式 ID 待申请
    device_id: int = 0x1000
    bar0_size: str = "1MiB"
    bar2_size: str = "64MiB"
    bar2_window_count: int = 16
    msix_table_size: int = 32
    link_speed: int = 4         # 当前链路速率：Gen4
    link_width: int = 4


@dataclass(frozen=True)
class DevmemParams:
    """设备内存（LPDDR5X）的范围。"""

    base: int = 0x0
    size: str = "8GiB"


@dataclass(frozen=True)
class AddrParams:
    """addr.* 参数。"""

    device_pa_bits: int = 40


@dataclass(frozen=True)
class Gw100Config:
    """一组 GW100 设备参数。"""

    pcie: PcieParams = field(default_factory=PcieParams)
    devmem: DevmemParams = field(default_factory=DevmemParams)
    addr: AddrParams = field(default_factory=AddrParams)
    hdi_max_contexts: int = 16  # hdi.max_contexts
    hdma_channels: int = 8      # hdma.channels
    # 需要板卡提供的能力，入口脚本据此拒绝不兼容的组合
    requires: frozenset = frozenset({"pcie"})

    def make(self) -> Gw100PciDevice:
        """按本组参数创建一个 GW100 endpoint 及其设备内存。"""
        p = self.pcie
        win_size = toMemorySize(p.bar2_size) // p.bar2_window_count
        dev = Gw100PciDevice(
            VendorID=p.vendor_id,
            DeviceID=p.device_id,
            BAR0=PciMemBar(size=p.bar0_size),
            BAR2=Gw100PrefetchMemBar(size=p.bar2_size),
            BAR3=PciMemUpperBar(),
            bar2_window_count=p.bar2_window_count,
            bar2_window_size=f"{win_size}B",
            msix_table_size=p.msix_table_size,
            link_speed=p.link_speed,
            link_width=p.link_width,
            hdi_max_contexts=self.hdi_max_contexts,
            hdma_channels=self.hdma_channels,
            devmem_base=self.devmem.base,
            devmem_size=self.devmem.size,
            device_pa_bits=self.addr.device_pa_bits,
        )
        dev.devmem = SimpleMemory(
            range=AddrRange(start=self.devmem.base, size=self.devmem.size),
            in_addr_map=False,
            kvm_map=False,
            conf_table_reported=False,
        )
        dev.mem_port = dev.devmem.port
        return dev


GW100 = Gw100Config()
