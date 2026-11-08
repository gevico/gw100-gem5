"""
@file  Gw100Sram.py
@brief 共享 SRAM 的时钟与端口配置
@see spec-sram
"""

from m5.objects.ClockedObject import ClockedObject
from m5.params import *


class Gw100Sram(ClockedObject):
    type = "Gw100Sram"
    cxx_class = "gem5::gw100::Gw100Sram"
    cxx_header = "src/gw100/sram/gw100_sram.hh"
    ports = VectorResponsePort("硬件绑定的 SRAM 逻辑入口")
    base = Param.Addr(0, "实例地址范围起点")
    size = Param.MemorySize("2MiB", "数据容量")
    banks = Param.Unsigned(8, "bank 数")
    word_bytes = Param.Unsigned(8, "每个宏的数据字宽")
    sources = Param.Unsigned(3, "逻辑入口数，包含预留入口")
    slots = Param.Unsigned(8, "每入口在途事务槽")
    read_cycles = Param.Unsigned(1, "读流水周期")
    write_cycles = Param.Unsigned(1, "写提交周期")
    interval_cycles = Param.Unsigned(1, "物理端口启动间隔")
    mask_bytes = Param.Unsigned(8, "宏写掩码粒度")
    interleaved = Param.Bool(False, "按宏字交织 bank")
    split_ports = Param.Bool(False, "每 bank 使用独立读写口")
    ecc = Param.Bool(True, "64+8 SECDED")
    trace_file = Param.String("", "可选 JSONL 事件记录")
    fault_offset = Param.Addr(0, "夹具在指定宏字提交后注入数据错误")
    fault_mask = Param.UInt64(0, "一次性数据位翻转掩码")
    fault_stage = Param.String(
        "commit", "注入点为 commit、issue_read、rmw 或 fetch"
    )
    fault_source = Param.Unsigned(0, "故障注入的硬件来源入口")
    fault_any_word = Param.Bool(False, "在选定来源的首个匹配宏字注入")
