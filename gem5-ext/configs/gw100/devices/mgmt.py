"""
@file  mgmt.py
@brief 管理 Tile 所在的管理系统（RISC-V）的创建

管理 Tile 运行真实的 RISC-V 启动 ROM 和固件，与 host 不在同一个系统：
这里创建一个 RiscvSystem，包含管理 CPU（RV64，M/U 模式，不带向量扩展）、
本地互连、管理 Tile、CLINT、PLIC，挂到 Root 下，与 host 系统在同一个
仿真进程中运行。管理 Tile 经 ML 与 PCIe 设备相连。

本地地址（spec-mgmt-tile §3.3）：ROM 0x0、SRAM 0x1000_0000、CLINT
0x2000_0000、本地寄存器 0x2000_C000、信箱 0x2001_0000、PLIC
0x2C00_0000、HIF 寄存器区 0x3000_0000。

@see spec-mgmt-tile, spec-mgmt-link, spec-clock-reset-power
"""

from m5.objects import (
    AddrRange,
    Clint,
    Gw100MgmtInterrupts,
    Gw100MgmtTile,
    NoncoherentXBar,
    Plic,
    RiscvBareMetal,
    RiscvISA,
    RiscvRTC,
    RiscvSemihosting,
    RiscvSystem,
    RiscvTimingSimpleCPU,
    SrcClockDomain,
    VoltageDomain,
)

# 复位后 clk_mgmt 选择 XTAL（spec-clock-reset-power §3.4）
XTAL_FREQ = "25MHz"
# 固件切换到 PLL_CORE 后的频率 mgmt.cpu_freq（spec-mgmt-tile §3.2）
MGMT_PLL_FREQ = "500MHz"
# mtime 与设备时间戳是同一个计数器（spec-mgmt-tile §7.3）
TIMESTAMP_FREQ = "100MHz"


def make_mgmt_system(link, rom_elf: str) -> RiscvSystem:
    """创建管理系统；link 为管理链路，rom_elf 为启动 ROM 的镜像。"""
    s = RiscvSystem()
    s.voltage_domain = VoltageDomain()
    # 频率档 0 为 PLL_CORE 分频后的工作频率，1 为 XTAL；复位后在档 1，
    # 由固件写 CRC_CLK_SEL 切换到档 0（spec-clock-reset-power §3.4）
    s.clk_domain = SrcClockDomain(
        clock=[MGMT_PLL_FREQ, XTAL_FREQ],
        init_perf_level=1,
        voltage_domain=s.voltage_domain,
    )
    s.mem_mode = "timing"
    s.mem_ranges = [
        AddrRange(0x0, size="64KiB"),
        AddrRange(0x1000_0000, size="2MiB"),
    ]

    # 本地互连是管理 Tile 的一部分，不额外计延迟：CPU 访问 SRAM（无
    # bank 冲突）为单周期（spec-mgmt-tile §10 P2）
    s.membus = NoncoherentXBar(
        width=16, frontend_latency=0, forward_latency=0, response_latency=0
    )
    s.system_port = s.membus.cpu_side_ports

    s.cpu = RiscvTimingSimpleCPU()
    s.cpu.isa = [RiscvISA(riscv_type="RV64", enable_rvv=False,
                          privilege_mode_set="MU")]
    s.cpu.createThreads()
    # 中断控制器加上管理 Tile 的停机请求（看门狗第二级、设备完全复位）
    s.cpu.interrupts = [Gw100MgmtInterrupts()]
    s.cpu.connectBus(s.membus)

    s.plic = Plic(pio_addr=0x2C00_0000, pio_size=0x400_0000, n_src=32,
                  hart_config="M")
    s.plic.pio = s.membus.mem_side_ports

    s.rtc = RiscvRTC(frequency=TIMESTAMP_FREQ)
    s.clint = Clint(pio_addr=0x2000_0000, num_threads=1)
    s.clint.int_pin = s.rtc.int_pin
    s.clint.pio = s.membus.mem_side_ports

    s.tile = Gw100MgmtTile(
        mgmt_link=link, plic=s.plic, cpu=s.cpu, pll_perf_level=0
    )
    s.tile.pio = s.membus.mem_side_ports
    s.tile.timestamp_reset = s.clint.reset

    s.workload = RiscvBareMetal(
        bootloader=rom_elf, semihosting=RiscvSemihosting()
    )
    return s
