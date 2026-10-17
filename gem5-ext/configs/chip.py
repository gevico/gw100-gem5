"""
@file  chip.py
@brief 按架构参数搭建整颗芯片

输入是 arch_params 转成的 JSON。

v0 的片上网络先用 gem5 的 NoncoherentXBar 代替（TODO：换成自研的 2D Mesh 模型，见 SPEC-120）。
连接关系：
  每个 Compute Tile：DMA.local、向量引擎 -> 本 Tile SRAM；DMA.remote -> NoC；
                    NoC -> 本 Tile SRAM（其他 Tile 和 TestHost 经此访问）；
                    指令中的本地窗口地址由 Tile 控制器换算成本 Tile SRAM 的全局地址
  8 个 DRAM 通道、Host 内存、派发器寄存器 -> NoC；派发器 DMA、TestHost -> NoC

@see SPEC-100
"""

import math
import re

from lpddr5x import LPDDR5X_8533_1x16_BG_BL32
from m5.objects import (AcisBankedSram, AcisDispatcher, AcisDmaEngine, AcisSyncUnit,
                        AcisTileController, AcisVectorEngine, AddrRange, MemCtrl,
                        NoncoherentXBar, SimpleMemory, SrcClockDomain, SubSystem, System,
                        VoltageDomain)


def compute_tile_positions(p):
    out = []
    for y, row in enumerate(p["mesh"]["layout"]):
        for x, kind in enumerate(row):
            if kind == "CT":
                out.append((x, y))
    return out


def build_chip(p, functional=True):
    am = p["address_map"]
    ct = p["compute_tile"]
    mt = p["memory_tile"]

    system = System(mem_mode="timing", cache_line_size=64)
    system.clk_domain = SrcClockDomain(clock=f"{p['clock']['freq_ghz']}GHz",
                                       voltage_domain=VoltageDomain())

    system.noc = NoncoherentXBar(
        width=p["noc"]["link_bytes_per_cycle"],
        frontend_latency=p["noc"]["router_latency_cycles"],
        forward_latency=p["noc"]["link_latency_cycles"],
        response_latency=p["noc"]["link_latency_cycles"],
    )
    system.system_port = system.noc.cpu_side_ports

    # ---------------------------------------------------------------- Compute Tile
    # 每个 Tile 一个 SubSystem 容器：先挂好子对象，再互相引用，避免参数引用把孤儿对象
    # 收养到别处。层次为 system.ct[i].{sram, sync, ve, dma, ctrl}。
    sram_size = ct["sram"]["size_kib"] * 1024
    # 指令里的 Tile 相对地址窗口，只在 Tile 内部译码，不挂到 NoC 上
    local_window = AddrRange(am["local_sram_base"], size=am["local_sram_size"])
    tiles = []
    for tid, _ in enumerate(compute_tile_positions(p)):
        base = am["tile_sram_base"] + tid * am["tile_sram_stride"]
        local = AddrRange(base, size=sram_size)
        t = SubSystem()
        t.sram = AcisBankedSram(range=local, banks=ct["sram"]["banks"],
                                bank_bytes=ct["sram"]["bank_bytes"],
                                latency=ct["sram"]["latency_cycles"])
        t.sync = AcisSyncUnit(counters=ct["sync"]["counters"])
        common = dict(local_range=local, functional_compute=functional,
                      queue_depth=ct["inst_queue"]["depth_per_unit"],
                      req_bytes=ct["dma"]["request_bytes"],
                      max_outstanding=ct["dma"]["max_outstanding"])
        t.ve = AcisVectorEngine(lanes=ct["vector_engine"]["lanes"],
                                pipeline_latency=ct["vector_engine"]["pipeline_latency_cycles"],
                                **common)
        t.dma = AcisDmaEngine(**common)
        t.ve.sync = t.sync
        t.dma.sync = t.sync
        t.ctrl = AcisTileController(tile_id=tid,
                                    ctrl_queue_depth=ct["inst_queue"]["depth_per_unit"],
                                    local_window=local_window, sram_range=local)
        t.ctrl.sync = t.sync
        t.ctrl.dma = t.dma
        t.ctrl.ve = t.ve
        tiles.append(t)
    system.ct = tiles
    for t in system.ct:
        t.ve.mem = t.sram.port
        t.dma.local = t.sram.port
        t.dma.remote = system.noc.cpu_side_ports
        system.noc.mem_side_ports = t.sram.port

    # ---------------------------------------------------------------- Memory Tile（8 条 DRAM 通道）
    n_mem_tiles = sum(1 for row in p["mesh"]["layout"] for k in row
                      if re.fullmatch(r"M\d+", k))
    channels = n_mem_tiles * mt["channels_per_tile"]
    intlv_bits = int(math.log2(channels))
    intlv_low = int(math.log2(am["dram_interleave_bytes"]))
    # DRAM 容量与器件规格只来自架构参数；测试不得为了省内存而缩小（后备存储按需分配）
    size = am["dram_size"]
    mem_ctrls = []
    for i in range(channels):
        ctrl = MemCtrl()
        # 每条 x16 通道一颗器件，器件容量取该通道分到的地址空间
        ctrl.dram = LPDDR5X_8533_1x16_BG_BL32(
            device_size=f"{size // channels // (1 << 20)}MiB",
            range=AddrRange(am["dram_base"], size=size,
                            intlvHighBit=intlv_low + intlv_bits - 1,
                            intlvBits=intlv_bits, intlvMatch=i))
        ctrl.port = system.noc.mem_side_ports
        mem_ctrls.append(ctrl)
    system.mem_ctrls = mem_ctrls

    # ---------------------------------------------------------------- Host 内存（v0 不建 PCIe 时序）
    system.host_mem = SimpleMemory(range=AddrRange(am["host_mem_base"], size=am["host_mem_size"]),
                                   latency="100ns")
    system.host_mem.port = system.noc.mem_side_ports

    # ---------------------------------------------------------------- 管理 Tile
    ic = p["mgmt_tile"]["inst_channel"]
    system.dispatcher = AcisDispatcher(pio_addr=am["mgmt_mmio_base"], controllers=[t.ctrl for t in system.ct],
                                       inst_latency=ic["latency_cycles"],
                                       inst_bytes_per_cycle=ic["bytes_per_cycle"])
    system.dispatcher.pio = system.noc.mem_side_ports
    system.dispatcher.dma = system.noc.cpu_side_ports

    system.mem_ranges = [AddrRange(am["dram_base"], size=size)]
    return system
