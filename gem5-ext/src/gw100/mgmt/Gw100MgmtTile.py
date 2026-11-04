"""
@file  Gw100MgmtTile.py
@brief 管理 Tile 的 SimObject 声明

管理 Tile 挂在管理系统（RiscvSystem）的本地总线上，经 pio 端口响应
管理 CPU 对启动 ROM、SRAM、本地寄存器、doorbell 信箱与事件寄存器、
HIF 寄存器区（经 ML）的访问；同时是 ML 管理侧的目标。CLINT 和 PLIC
是单独的设备。

@see spec-mgmt-tile, spec-mgmt-link
"""

from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *


class Gw100MgmtTile(ClockedObject):
    """管理 Tile：ROM、SRAM、本地寄存器、信箱，以及 ML 的管理侧。"""

    type = "Gw100MgmtTile"
    cxx_header = "src/gw100/mgmt/mgmt_tile.hh"
    cxx_class = "gem5::gw100::Gw100MgmtTile"

    pio = ResponsePort("接管理系统的本地总线")
    wdt_fatal = IntSourcePin(
        "看门狗第二级超时的专用信号，接 PCIe 设备的 wdt_fatal"
        "（spec-mgmt-tile §7.4）"
    )

    rom_size = Param.MemorySize("64KiB", "启动 ROM（mgmt.rom_size）")
    sram_size = Param.MemorySize("2MiB", "SRAM（mgmt.sram_size）")
    sram_banks = Param.Unsigned(8, "SRAM 的 bank 数")
    fw_load_max = Param.MemorySize("1MiB", "装载区（mgmt.fw_load_max）")
    db_slots = Param.Unsigned(34, "doorbell 槽位数（mgmt.db_slots）")

    pll_lock = Param.Latency(
        "500us",
        "PLL 的锁定时间（推算）；不超过 clock.pll_lock_timeout = 1 ms",
    )
    crc_access = Param.Latency(
        "200ns", "CRC 寄存器一次访问的握手延迟：约 5 个 XTAL 周期（推算）"
    )
    clk_switch_pause = Param.Latency(
        "84ns",
        "无毛刺时钟选择器切换时的暂停：2 个 XTAL 加 2 个 PLL 周期（推算），"
        "不超过 clock.clk_switch_pause",
    )
    pll_perf_level = Param.Unsigned(
        0, "clk_mgmt 切换到 PLL_CORE 后，管理系统时钟域所用的频率档"
    )

    timestamp_freq = Param.Frequency(
        "100MHz", "mtime 的频率（dev.timestamp_hz），看门狗以它计时"
    )
    ts_sync = Param.Cycles(
        2, "mtime 经同步器进入 clk_mgmt 域的延迟（两级同步器，推算）"
    )
    cpu = Param.BaseCPU(NULL, "管理 CPU；看门狗第二级超时时停住它")

    mgmt_link = Param.Gw100MgmtLink("管理链路 ML")
    plic = Param.PlicBase("接收本 Tile 中断的 PLIC")
