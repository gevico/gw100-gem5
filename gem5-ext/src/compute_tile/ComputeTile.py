"""
@file  ComputeTile.py
@brief Compute Tile 功能单元与 Tile 控制器的 SimObject 声明

@see SPEC-200
"""

from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *


class Gw100FunctionalUnit(ClockedObject):
    """功能单元的公共基类（SPEC-200）。"""

    type = "Gw100FunctionalUnit"
    abstract = True
    cxx_header = "src/compute_tile/functional_unit.hh"
    cxx_class = "gem5::gw100::FunctionalUnit"

    system = Param.System(Parent.any, "所属系统，用于分配请求者编号")
    sync = Param.Gw100SyncUnit("本 Tile 的同步单元")
    queue_depth = Param.Unsigned(16, "指令队列深度")
    functional_compute = Param.Bool(True, "是否真正计算结果；只跑时序时关闭")
    req_bytes = Param.Unsigned(64, "单个访存请求的最大字节数")
    max_outstanding = Param.Unsigned(32, "每个数据流同时在途的请求数上限")
    local_range = Param.AddrRange("本 Tile SRAM 的地址范围")


class Gw100VectorEngine(Gw100FunctionalUnit):
    """向量引擎（SPEC-220）。"""

    type = "Gw100VectorEngine"
    cxx_header = "src/compute_tile/vector/vector_engine.hh"
    cxx_class = "gem5::gw100::VectorEngine"

    mem = RequestPort("连到本 Tile SRAM")
    lanes = Param.Unsigned(32, "每周期处理的元素数")
    pipeline_latency = Param.Cycles(8, "流水线延迟")


class Gw100DmaEngine(Gw100FunctionalUnit):
    """Compute Tile 的 DMA（SPEC-250 / 320）。"""

    type = "Gw100DmaEngine"
    cxx_header = "src/compute_tile/dma/dma_engine.hh"
    cxx_class = "gem5::gw100::DmaEngine"

    local = RequestPort("连到本 Tile SRAM")
    remote = RequestPort("连到片上网络，访问 DRAM 和其他 Tile")


class Gw100TileController(ClockedObject):
    """Tile 控制器：接收指令并分发到功能单元（SPEC-200 / 230）。"""

    type = "Gw100TileController"
    cxx_header = "src/compute_tile/controller/tile_controller.hh"
    cxx_class = "gem5::gw100::TileController"

    tile_id = Param.Unsigned("Compute Tile 编号（行优先）")
    sync = Param.Gw100SyncUnit("本 Tile 的同步单元")
    dma = Param.Gw100DmaEngine("本 Tile 的 DMA")
    ve = Param.Gw100VectorEngine("本 Tile 的向量引擎")
    me = Param.Gw100FunctionalUnit(NULL, "本 Tile 的矩阵引擎（v0 尚未实现）")
    ctrl_queue_depth = Param.Unsigned(16, "控制指令（NOP/SYNC/END）队列深度")
    local_window = Param.AddrRange("本地窗口：指令中的 Tile 相对地址（SPEC-110）")
    sram_range = Param.AddrRange("本 Tile SRAM 的全局地址范围")
