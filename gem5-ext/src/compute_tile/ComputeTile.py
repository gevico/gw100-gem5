from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *


class AcisFunctionalUnit(ClockedObject):
    """功能单元的公共基类（SPEC-200）。"""

    type = "AcisFunctionalUnit"
    abstract = True
    cxx_header = "src/compute_tile/functional_unit.hh"
    cxx_class = "gem5::acis::FunctionalUnit"

    system = Param.System(Parent.any, "所属系统，用于分配请求者编号")
    sync = Param.AcisSyncUnit("本 Tile 的同步单元")
    queue_depth = Param.Unsigned(16, "指令队列深度")
    functional_compute = Param.Bool(True, "是否真正计算结果；只跑时序时关闭")
    req_bytes = Param.Unsigned(64, "单个访存请求的最大字节数")
    max_outstanding = Param.Unsigned(32, "每个数据流同时在途的请求数上限")
    local_range = Param.AddrRange("本 Tile SRAM 的地址范围")


class AcisVectorEngine(AcisFunctionalUnit):
    """向量引擎（SPEC-220）。"""

    type = "AcisVectorEngine"
    cxx_header = "src/compute_tile/vector/vector_engine.hh"
    cxx_class = "gem5::acis::VectorEngine"

    mem = RequestPort("连到本 Tile SRAM")
    lanes = Param.Unsigned(32, "每周期处理的元素数")
    pipeline_latency = Param.Cycles(8, "流水线延迟")


class AcisDmaEngine(AcisFunctionalUnit):
    """Compute Tile 的 DMA（SPEC-250 / 320）。"""

    type = "AcisDmaEngine"
    cxx_header = "src/compute_tile/dma/dma_engine.hh"
    cxx_class = "gem5::acis::DmaEngine"

    local = RequestPort("连到本 Tile SRAM")
    remote = RequestPort("连到片上网络，访问 DRAM 和其他 Tile")


class AcisTileController(ClockedObject):
    """Tile 控制器：接收指令并分发到功能单元（SPEC-200 / 230）。"""

    type = "AcisTileController"
    cxx_header = "src/compute_tile/controller/tile_controller.hh"
    cxx_class = "gem5::acis::TileController"

    tile_id = Param.Unsigned("Compute Tile 编号（行优先）")
    sync = Param.AcisSyncUnit("本 Tile 的同步单元")
    dma = Param.AcisDmaEngine("本 Tile 的 DMA")
    ve = Param.AcisVectorEngine("本 Tile 的向量引擎")
    me = Param.AcisFunctionalUnit(NULL, "本 Tile 的矩阵引擎（v0 尚未实现）")
    ctrl_queue_depth = Param.Unsigned(16, "控制指令（NOP/SYNC/END）队列深度")
    local_window = Param.AddrRange("本地窗口：指令中的 Tile 相对地址（SPEC-110）")
    sram_range = Param.AddrRange("本 Tile SRAM 的全局地址范围")
