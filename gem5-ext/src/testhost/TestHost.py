"""
@file  TestHost.py
@brief TestHost 的 SimObject 声明

@see SPEC-400
"""

from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *


class AcisTestHost(ClockedObject):
    """测试平台：后门加载、前门启动、轮询完成、后门读回（不是硬件）。"""

    type = "AcisTestHost"
    cxx_header = "src/testhost/test_host.hh"
    cxx_class = "gem5::acis::TestHost"

    port = RequestPort("连到片上网络")
    system = Param.System(Parent.any, "所属系统")
    loads = VectorParam.String([], "后门加载：'addr=path'")
    dumps = VectorParam.String([], "后门读回：'addr:bytes=path'")
    mgmt_base = Param.Addr("管理 Tile 寄存器窗口基地址")
    cmd_record_addr = Param.Addr("命令记录所在地址")
    completion_addr = Param.Addr("完成记录所在地址")
    poll_interval = Param.Latency("100ns", "轮询完成记录的间隔")
    timeout = Param.Latency("100ms", "超时时间；0 表示不设超时")
    result_file = Param.String("", "结果 JSON 的路径")
    backdoor_chunk_bytes = Param.Unsigned(
        64, "后门访问按此粒度对齐切分（2 的幂，不大于 DRAM 交织粒度）")
