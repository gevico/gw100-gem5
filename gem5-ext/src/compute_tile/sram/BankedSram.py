"""
@file  BankedSram.py
@brief Tile SRAM 的 SimObject 声明

@see SPEC-200
"""

from m5.objects.AbstractMemory import AbstractMemory
from m5.params import *


class Gw100BankedSram(AbstractMemory):
    """Compute Tile 内的共享 SRAM：多端口、分 bank（SPEC-200）。"""

    type = "Gw100BankedSram"
    cxx_header = "src/compute_tile/sram/banked_sram.hh"
    cxx_class = "gem5::gw100::BankedSram"

    port = VectorResponsePort("访问端口；各功能单元和 NIU 各连一个")
    banks = Param.Unsigned(32, "bank 数")
    bank_bytes = Param.Unsigned(32, "每个 bank 每周期的字节数，也是交织粒度")
    latency = Param.Cycles(4, "访问延迟")
