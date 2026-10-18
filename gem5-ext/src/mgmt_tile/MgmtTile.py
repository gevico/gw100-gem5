"""
@file  MgmtTile.py
@brief 管理 Tile 派发器的 SimObject 声明

@see SPEC-330, SPEC-400
"""

from m5.objects.Device import DmaDevice
from m5.params import *


class Gw100Dispatcher(DmaDevice):
    """管理 Tile 的派发器：命令队列、doorbell、集中派发张量指令（SPEC-330 / 400）。"""

    type = "Gw100Dispatcher"
    cxx_header = "src/mgmt_tile/dispatcher.hh"
    cxx_class = "gem5::gw100::Dispatcher"

    pio_addr = Param.Addr("寄存器窗口基地址（address_map.mgmt_mmio_base）")
    pio_latency = Param.Latency("20ns", "寄存器访问延迟")
    controllers = VectorParam.Gw100TileController("各 Compute Tile 的控制器，下标即 Tile 编号")
    inst_latency = Param.Cycles(10, "指令通道延迟")
    inst_bytes_per_cycle = Param.Unsigned(16, "指令通道每周期传输的字节数")
