from m5.objects.ClockedObject import ClockedObject
from m5.params import *


class AcisSyncUnit(ClockedObject):
    """同步单元：计数器的更新与等待（SPEC-250）。"""

    type = "AcisSyncUnit"
    cxx_header = "src/compute_tile/sync/sync_unit.hh"
    cxx_class = "gem5::acis::SyncUnit"

    counters = Param.Unsigned(32, "计数器个数")
    latency = Param.Cycles(1, "条件满足到唤醒等待者的延迟")
