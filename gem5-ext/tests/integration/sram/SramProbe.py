"""
@file  SramProbe.py
@brief SRAM 多发起者与恢复验证夹具
@see spec-sram
"""

from m5.objects.ClockedObject import ClockedObject
from m5.params import *


class SramProbe(ClockedObject):
    type = "SramProbe"
    cxx_class = "gem5::gw100::SramProbe"
    cxx_header = "tests/integration/sram/sram_probe.hh"
    ports = VectorRequestPort("有限的引擎访问入口")
    sram = Param.Gw100Sram("待测共享 SRAM")
    snapshot = Param.Bool(False, "在排空后请求 checkpoint")
    retention = Param.Bool(False, "checkpoint 前进入保持")
    stall_cycles = Param.Unsigned(40, "首次响应的阻塞周期")
    reset_test = Param.Bool(False, "在写发出后同 tick 复位")
    clock_switch = Param.Bool(False, "在途操作暂停并由 25 MHz 切到正常时钟")
