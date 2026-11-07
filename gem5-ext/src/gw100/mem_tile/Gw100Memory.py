"""
@file  Gw100Memory.py
@brief Memory Tile 与临时 timing 入口的 SimObject 声明

@see spec-dram, spec-clock-reset-power
"""

from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *
from m5.SimObject import PyBindMethod


class Gw100MemoryTile(ClockedObject):
    """两个独立通道、控制寄存器和全局 DPA 保护。"""

    type = "Gw100MemoryTile"
    cxx_header = "src/gw100/mem_tile/memory_tile.hh"
    cxx_class = "gem5::gw100::Gw100MemoryTile"
    cpu_side = ResponsePort("临时互连到 Memory Tile")
    mem_side = VectorRequestPort("两个原生 MemCtrl 的 timing 端口")
    tile_id = Param.Unsigned("Memory Tile 编号")
    controllers = VectorParam.MemCtrl("两个原生控制器")
    ddr_domain = Param.ClockDomain("DDR 控制器时钟")
    calibration_latency = Param.Latency("校准反馈时延")
    training_latency = Param.Latency("训练反馈时延")
    fail_mask = Param.UInt64(0, "全局逐 rank 的反馈失败掩码")
    timeout_mask = Param.UInt64(0, "全局逐 rank 的反馈无响应掩码")
    fault_phase = Param.Unsigned(3, "故障阶段，2 校准、3 训练")
    corrupt_selftest = Param.Bool(False, "自检读取故障注入")
    selftest_fail_mask = Param.UInt64(0, "全局逐 rank 的自检失败掩码")
    cxx_exports = [PyBindMethod("readRegister")]


class Gw100MemoryIngress(ClockedObject):
    """可信来源入口与跨域序列化，限制在途和返回占用。"""

    type = "Gw100MemoryIngress"
    cxx_header = "src/gw100/mem_tile/memory_ingress.hh"
    cxx_class = "gem5::gw100::Gw100MemoryIngress"
    cpu_side = ResponsePort("来源端口")
    mem_side = RequestPort("连接临时核心互连")
    source_id = Param.Unsigned("硬件来源编号")
    source_domain = Param.ClockDomain("来源时钟域")
    system = Param.System(Parent.any, "独立的设备 timing System")
    tiles = VectorParam.Gw100MemoryTile("用于完整请求权限检查的四个 Tile")
    cxx_exports = [PyBindMethod("metric")]
