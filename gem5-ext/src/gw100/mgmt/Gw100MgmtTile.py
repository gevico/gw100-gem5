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
from m5.SimObject import PyBindMethod


class Gw100MgmtTile(ClockedObject):
    """管理 Tile：ROM、SRAM、本地寄存器、信箱，以及 ML 的管理侧。"""

    type = "Gw100MgmtTile"
    cxx_header = "src/gw100/mgmt/mgmt_tile.hh"
    cxx_class = "gem5::gw100::Gw100MgmtTile"

    pio = ResponsePort("接管理系统的本地总线")
    ni = RequestPort(
        "NI 发起端口：管理 CPU 经它访问 DPA 空间（本地地址 0x100_0000_0000 + "
        "DPA，spec-mgmt-tile §3.3）"
    )
    device_pa_bits = Param.Unsigned(40, "addr.device_pa_bits")
    wdt_fatal = IntSourcePin(
        "看门狗第二级超时的专用信号，接 PCIe 设备的 wdt_fatal"
        "（spec-mgmt-tile §7.4）"
    )
    dev_reset_req = IntSinkPin(
        "设备完全复位的请求，接 PCIe 设备的 dev_reset_req"
    )
    pcie_reset = ResetRequestPort(
        "送给 PCIe 设备的功能逻辑复位信号，接它的 func_reset"
    )
    timestamp_reset = ResetRequestPort("冷复位和热复位时清零 CLINT")
    reset_endpoint = Param.Gw100PciDevice(NULL, "CRC 复位的 PCIe endpoint")
    cxx_exports = [PyBindMethod("externalReset")]

    rom_size = Param.MemorySize("64KiB", "启动 ROM（mgmt.rom_size）")
    sram_size = Param.MemorySize("2MiB", "SRAM（mgmt.sram_size）")
    sram_banks = Param.Unsigned(8, "SRAM 的 bank 数")
    sram = Param.Gw100Sram("共享 SRAM 实例")
    sram_cpu = RequestPort("CPU 的 SRAM timing 入口")
    sram_ml = RequestPort("ML 的 SRAM timing 入口")
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
    cpu = Param.BaseCPU(
        NULL,
        "管理 CPU；看门狗第二级超时和设备完全复位时停住它，中断控制器"
        "必须是 Gw100MgmtInterrupts",
    )
    dev_reset_sync = Param.Latency(
        "80ns", "DEV_RESET 请求经同步器进入 clk_ao 域：2 个 XTAL 周期（推算）"
    )
    dev_reset_hold = Param.Latency(
        "640ns", "设备完全复位的复位保持时间：16 个 XTAL 周期（推算）"
    )
    xtal_perf_level = Param.Unsigned(
        1, "clk_mgmt 选择 XTAL 时，管理系统时钟域所用的频率档"
    )

    mgmt_link = Param.Gw100MgmtLink("管理链路 ML")
    memory_tiles = VectorParam.Gw100MemoryTile([], "CRC 控制的内存 Tile")
    memory_ingresses = VectorParam.Gw100MemoryIngress([], "临时连接入口")
    memory_core_domain = Param.SrcClockDomain(NULL, "临时互连时钟")
    memory_ddr_domain = Param.SrcClockDomain(NULL, "DDR 控制器时钟")
    plic = Param.PlicBase("接收本 Tile 中断的 PLIC")
