"""
@file  Gw100MgmtInterrupts.py
@brief 管理 CPU 的中断控制器的 SimObject 声明

在 gem5 的 RISC-V 中断控制器上加一个停机请求：看门狗第二级超时和设备
完全复位时，管理 Tile 经它让 CPU 在指令边界停住，停机优先于中断。

@see spec-mgmt-tile
"""

from m5.objects.RiscvInterrupts import RiscvInterrupts


class Gw100MgmtInterrupts(RiscvInterrupts):
    """RISC-V 中断控制器，加上管理 Tile 的停机请求。"""

    type = "Gw100MgmtInterrupts"
    cxx_header = "src/gw100/mgmt/intr/mgmt_interrupts.hh"
    cxx_class = "gem5::gw100::MgmtInterrupts"
