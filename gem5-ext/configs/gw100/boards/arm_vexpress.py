"""
@file  arm_vexpress.py
@brief AArch64 host 板卡（VExpress_GEM5_V2）

默认走 bare-metal：直接加载一个裸机 ELF，不启动 Linux，
也不生成 DTB 或使用 bootloader。Linux 模式保留，供后续对比。
CPU、内存和 cache 由 host 预设提供；板卡不认识具体设备，
只把传进来的 PCI 设备依次挂到 PCIe 总线上。

平台带 GICv3（GICv4）和 ITS（0x2E01_0000），可以把设备的 MSI/MSI-X
翻译成 LPI 交给 CPU。PCI 的配置空间（ECAM）在 0x3000_0000，内存窗口只有
一个：总线地址 0 – 0x3FFF_FFFF 对应 CPU 地址 0x4000_0000 – 0x7FFF_FFFF；
INTx 经 GIC 的 SPI（int_base 100 加设备号模 4）。地址见
gem5/src/dev/arm/RealView.py 的 VExpress_GEM5_Base、VExpress_GEM5_V2_Base。

bare-metal 时 host 的 MMU 关闭，CPU 以非 cache 方式访问内存；gem5 的非
cache 访问不会探查 IO cache，设备 DMA 写入、仍留在 IO cache 中的数据
对 CPU 不可见。因此 bare-metal 时不建 IO cache，I/O 总线经一个只转发
内存地址范围的 bridge 接一致性内存总线（与 gem5 无 cache 时的 DMA
bridge 相同，延迟 50 ns），DMA 写入直接进入内存系统。这个 bridge 还转发 ITS
的翻译页（GITS_TRANSLATER 所在的 64 KiB），PCI 设备的 MSI/MSI-X 写才能
到达挂在内存总线上的 ITS；ITS 的控制页不对设备开放。
Linux 模式保留 IO cache，此时 CPU 的 cache 访问会探查它。

@see spec-arch, spec-pcie
"""

from pathlib import Path

from m5.objects import (
    AddrRange,
    ArmDefaultRelease,
    ArmSemihosting,
    Bridge,
    VExpress_GEM5_V2,
)

from gem5.components.boards.abstract_board import AbstractBoard
from gem5.components.boards.arm_board import ArmBoard
from gem5.components.cachehierarchies.classic.\
    private_l1_private_l2_cache_hierarchy import (
        PrivateL1PrivateL2CacheHierarchy,
    )
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA

from ..hosts import HostConfig
from ..workload import ArmBaremetal


class _BaremetalCacheHierarchy(PrivateL1PrivateL2CacheHierarchy):
    """bare-metal 用的 cache 层次：不建 IO cache（见文件头）。"""

    def _setup_io_cache(self, board: AbstractBoard) -> None:
        """I/O 总线经 DMA bridge 接一致性内存总线（内存与 ITS 翻译页）。"""
        its = board.realview.gic.its
        translate = AddrRange(its.pio_addr + 0x10000, size=0x10000)
        self.dmabridge = Bridge(
            delay="50ns", ranges=list(board.mem_ranges) + [translate]
        )
        self.dmabridge.cpu_side_port = board.get_mem_side_coherent_io_port()
        self.dmabridge.mem_side_port = self.membus.cpu_side_ports


class ArmVExpressBoard(ArmBoard):
    """VExpress 平台的 ARM 全系统板卡，PCIe 上挂传入的设备。"""

    @classmethod
    def features(cls) -> frozenset:
        """本板卡提供的能力；SimObject 类上不能放普通类属性，用方法返回。"""
        return frozenset({"pcie"})

    def __init__(
        self,
        host: HostConfig,
        pci_devices: list,
        bare_metal: bool = False,
    ):
        self._extra_pci_devices = list(pci_devices)
        self._bare_metal = bare_metal
        self._kernel = None
        self._wait_gdb = False
        self._gdb_port = 7000
        self._devices_attached = False
        # 裸机先单核，避免多核同时从 reset 入口执行。
        num_cores = 1 if bare_metal else host.num_cores
        super().__init__(
            clk_freq=host.clk_freq,
            processor=SimpleProcessor(
                cpu_type=host.cpu_type,
                isa=ISA.ARM,
                num_cores=num_cores,
            ),
            memory=DualChannelDDR4_2400(size=host.mem_size),
            cache_hierarchy=(
                _BaremetalCacheHierarchy if bare_metal
                else PrivateL1PrivateL2CacheHierarchy
            )(
                l1d_size=host.l1d_size,
                l1i_size=host.l1i_size,
                l2_size=host.l2_size,
            ),
            platform=VExpress_GEM5_V2(),
            release=ArmDefaultRelease(),
        )

    def set_baremetal_workload(
        self, kernel: str, wait_gdb: bool = False, gdb_port: int = 7000
    ) -> None:
        """加载裸机 ELF；调用后不能再设置 Linux workload。

        远程 GDB 监听 127.0.0.1 的 gdb_port；wait_gdb 为真时，仿真开始前
        等待 GDB 连接，用于从第一条指令起调试裸机程序。
        """
        path = Path(kernel)
        if not path.is_file():
            raise FileNotFoundError(f"bare-metal ELF 不存在：{path}")
        self._bare_metal = True
        self._kernel = str(path)
        self._wait_gdb = wait_gdb
        self._gdb_port = gdb_port
        self._set_fullsystem(True)

    def _setup_board(self) -> None:
        super()._setup_board()
        if self._bare_metal:
            self.workload = ArmBaremetal(
                self._kernel,
                self,
                wait_for_remote_gdb=self._wait_gdb,
                remote_gdb_port=self._gdb_port,
            )
            self.semihosting = ArmSemihosting()
            self.auto_reset_addr = True

    def _pre_instantiate(self, full_system=None):
        if not self._devices_attached:
            self._devices_attached = True
            # 设备号由 VExpress 的 attachPciDevice 从 1 起依次分配
            for dev in self._extra_pci_devices:
                self._pci_devices.append(dev)
                self.realview.attachPciDevice(dev)

        # bare-metal 不生成 DTB、不加载 bootloader，只做通用连接
        if self._bare_metal:
            self.pci_devices = self._pci_devices
            return AbstractBoard._pre_instantiate(
                self, full_system=full_system
            )
        return super()._pre_instantiate(full_system=full_system)

    def _setup_io_devices(self):
        super()._setup_io_devices()
