"""
@file  arm_vexpress.py
@brief AArch64 host 板卡（VExpress_GEM5_Foundation）

默认走 bare-metal：直接加载一个裸机 ELF，不启动 Linux，
也不生成 DTB 或使用 bootloader。Linux 模式保留，供后续对比。
CPU、内存和 cache 由 host 预设提供；板卡不认识具体设备，
只把传进来的 PCI 设备依次挂到 PCIe 总线上。

Foundation 平台没有接 GICv3 ITS，不提供 MSI/MSI-X。

@see spec-arch, spec-pcie
"""

from pathlib import Path

from m5.objects import (
    ArmDefaultRelease,
    ArmSemihosting,
    VExpress_GEM5_Foundation,
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
            cache_hierarchy=PrivateL1PrivateL2CacheHierarchy(
                l1d_size=host.l1d_size,
                l1i_size=host.l1i_size,
                l2_size=host.l2_size,
            ),
            platform=VExpress_GEM5_Foundation(),
            release=ArmDefaultRelease(),
        )

    def set_baremetal_workload(self, kernel: str) -> None:
        """加载裸机 ELF；调用后不能再设置 Linux workload。"""
        path = Path(kernel)
        if not path.is_file():
            raise FileNotFoundError(f"bare-metal ELF 不存在：{path}")
        self._bare_metal = True
        self._kernel = str(path)
        self._set_fullsystem(True)

    def _setup_board(self) -> None:
        super()._setup_board()
        if self._bare_metal:
            self.workload = ArmBaremetal(self._kernel, self)
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
