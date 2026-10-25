"""
@file  board_arm.py
@brief GW100 的 ARM host board

默认走 bare-metal：直接加载一个裸机 ELF，不启动 Linux，
也不生成 DTB 或使用 bootloader。Linux 模式保留，供后续对比。

@see SPEC-100, SPEC-310
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
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA

from .device import attach_gw100
from .params import Gw100Params
from .workload import ArmBaremetal


class Gw100ArmBoard(ArmBoard):
    """带一个 GW100 PCIe endpoint 的 ARM 全系统板卡。"""

    def __init__(self, params: Gw100Params, bare_metal: bool = False):
        self._gw100_params = params
        self._bare_metal = bare_metal
        self._kernel = None
        self._gw100_attached = False
        # 裸机先单核，避免多核同时从 reset 入口执行。
        num_cores = 1 if bare_metal else params.num_cores
        # 第一步用 O3 观察详细流水线；启动会明显变慢。
        super().__init__(
            clk_freq=params.clk_freq,
            processor=SimpleProcessor(
                cpu_type=CPUTypes.O3,
                isa=ISA.ARM,
                num_cores=num_cores,
            ),
            memory=DualChannelDDR4_2400(size=params.mem_size),
            cache_hierarchy=PrivateL1PrivateL2CacheHierarchy(
                l1d_size="64KiB",
                l1i_size="64KiB",
                l2_size="1MiB",
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
        if not self._gw100_attached:
            self._gw100_attached = True
            attach_gw100(self, self._gw100_params)

        # bare-metal 不生成 DTB、不加载 bootloader，只做通用连接
        if self._bare_metal:
            self.pci_devices = self._pci_devices
            return AbstractBoard._pre_instantiate(
                self, full_system=full_system
            )
        return super()._pre_instantiate(full_system=full_system)

    def _setup_io_devices(self):
        super()._setup_io_devices()
