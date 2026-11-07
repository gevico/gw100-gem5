"""
@file  gw100.py
@brief GW100 PCIe endpoint 的设备参数与创建

参数按规格中的名字分组（pcie.*、devmem.*、addr.*、hdi.*、hdma.*），单一
真源重建后整体迁移过去。这里创建设备和它的设备内存，挂到哪条 PCIe 总线
由板卡决定。

默认设备由 memory.py 在独立 timing System 内连接八通道 LPDDR5-8433。
管理 CPU、HDMA、BAR2 分别经过有限的来源入口。gw100_compat 使用
SimpleMemory 和功能 BAR2。临时交叉开关的适用范围见理想化清单。

@see spec-pcie, spec-hdi
"""

from dataclasses import dataclass, field, replace

from m5.objects import (
    AddrRange,
    Gw100MgmtLink,
    Gw100PciDevice,
    Gw100PrefetchMemBar,
    NoncoherentXBar,
    SimpleMemory,
    SrcClockDomain,
    VoltageDomain,
)
from m5.objects.PciDevice import PciMemBar, PciMemUpperBar
from m5.proxy import Parent
from m5.util.convert import toMemorySize

from .mgmt import make_mgmt_system

# clk_pcie：PCIe 控制器、入站完成器、BAR0 寄存器、HDMA
# （spec-clock-reset-power §3.2）
PCIE_FREQ = "250MHz"
# clk_core（spec-clock-reset-power §3.2）
CORE_FREQ = "1GHz"


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
    """设备内存的范围。"""

    base: int = 0x0
    size: str = "32GiB"


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
    precise_memory: bool = True
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
            async_memory=self.precise_memory,
            bar2_latency="8ns" if self.precise_memory else "200ns",
            devmem_max_outstanding=16 if self.precise_memory else 32,
        )
        dev.clk_domain = SrcClockDomain(
            clock=PCIE_FREQ, voltage_domain=VoltageDomain()
        )
        dev.mgmt_link = Gw100MgmtLink()
        if self.precise_memory:
            return dev
        dev.devmem = SimpleMemory(
            range=AddrRange(start=self.devmem.base, size=self.devmem.size),
            in_addr_map=False,
            kvm_map=False,
            conf_table_reported=False,
        )
        # 理想化的 NoC：clk_core 1 GHz，32 B 宽，每段 2 个周期（推算）
        dev.devnoc = NoncoherentXBar(
            width=32,
            frontend_latency=2,
            forward_latency=2,
            response_latency=2,
            clk_domain=SrcClockDomain(
                clock=CORE_FREQ, voltage_domain=VoltageDomain()
            ),
        )
        dev.mem_port = dev.devnoc.cpu_side_ports
        dev.devnoc.mem_side_ports = dev.devmem.port
        return dev

    def make_mgmt(self, dev: Gw100PciDevice, rom_elf: str):
        """创建与 dev 相连的管理系统（RiscvSystem），挂到 Root 下。"""
        s = make_mgmt_system(dev.mgmt_link, rom_elf)
        # 看门狗第二级超时的专用信号，不经过 ML（spec-mgmt-tile §7.4）
        s.tile.wdt_fatal = dev.wdt_fatal
        # 设备完全复位：PCIe 设备向 CRC 请求，CRC 复位 PCIe 的功能逻辑
        # （spec-clock-reset-power §4.3）
        s.tile.dev_reset_req = dev.dev_reset_req
        s.tile.pcie_reset = dev.func_reset
        # 延后解析，避免尚未挂到板卡的 endpoint 被管理 Tile 收为子对象。
        s.tile.reset_endpoint = Parent.board.pci_devices[0]
        # 管理 Tile 的 NI 经理想化的 NoC 访问设备内存
        if not self.precise_memory:
            s.tile.ni = dev.devnoc.cpu_side_ports
        s.tile.device_pa_bits = self.addr.device_pa_bits
        return s


GW100 = Gw100Config()
GW100_COMPAT = replace(GW100, precise_memory=False)
