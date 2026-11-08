"""
@file  MemoryProbe.py
@brief Memory Tile 组件测试的有界 timing 请求源

通过真实端口执行控制命令、数据比较、反压和 checkpoint 场景。
@see spec-dram
"""

from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *
from m5.SimObject import PyBindMethod


class Gw100MemoryProbe(ClockedObject):
    """独立组件测试的数字请求源和 CRC 输入替身。"""

    type = "Gw100MemoryProbe"
    cxx_header = "tests/integration/memory/memory_probe.hh"
    cxx_class = "gem5::gw100::Gw100MemoryProbe"
    ni = RequestPort("管理来源")
    mem_port = RequestPort("HDMA 来源")
    bar2_mem_port = RequestPort("BAR2 来源")
    memory_tiles = VectorParam.Gw100MemoryTile([], "被测 Tile")
    memory_ingresses = VectorParam.Gw100MemoryIngress([], "被测来源入口")
    memory_core_domain = Param.SrcClockDomain("临时核心时钟")
    memory_ddr_domain = Param.SrcClockDomain("DDR 时钟")
    operations = VectorParam.UInt64("每条为类型、来源、地址、大小、值")
    system = Param.System(Parent.any, "设备 timing System")
    max_outstanding = Param.Unsigned(64, "测试源在途上限")
    response_hold = Param.Latency("0ns", "每个响应施加的上游背压时间")
    cxx_exports = [PyBindMethod("checks"), PyBindMethod("retries")]
