"""
@file  multi_isa.py
@brief 多 ISA 可行性测试：ARM host、RV64 管理核与 16 个 RV32 核在同一进程中运行

可行性测试，不是产品组件：产品配置不导入本文件，RV32 核的系统结构
（独立的 RiscvSystem、64 KiB SimpleMemory、1 GHz）只为验证 gem5 能否
在一个进程中同时运行这些核，不代表 Compute Tile 的设计，也不登记理想化。

组成：
- ARM host 与 GW100 设备：与 run_gw100.py 相同的板卡、host 预设和设备
  预设；host 运行 --kernel 给出的裸机程序（推荐 boot_ok：经 BAR2 加载
  固件并等待 FW_READY，以此证明 RV64 管理核执行了启动 ROM 和固件）；
- RV64 管理核：设备预设中的管理系统，运行 --rom 给出的启动 ROM；
- N 个 RV32 核：每个核一个独立的 RiscvSystem（RV32IMAC 的 timing CPU、
  自己的交叉开关和存储），运行以 CORE_ID 编译的 rv32_hello_<k>.elf。

每个 RV32 核经 semihosting 输出一行结果，再以 SYS_EXIT_EXTENDED 退出，
子码为 0x100 | 失败位 0x80 | 核号；ARM 程序的退出子码为 0 通过、1 失败、
2 异常。本脚本逐个收集退出事件，直到 ARM 程序和全部 RV32 核都已报告
（某个核的退出不结束整个测试），然后从 stats.txt 读出每个核的提交
指令数，打印逐核的结果。全部通过时进程退出码为 0，否则为 1；到达
--max-ticks 为 124。

用法见同目录的 README.md。

@see man-gem5-user
"""

import argparse
import os
import re
import sys

import m5
from m5.objects import (
    AddrRange,
    NoncoherentXBar,
    RiscvBareMetal,
    RiscvISA,
    RiscvSemihosting,
    RiscvSystem,
    RiscvTimingSimpleCPU,
    Root,
    SimpleMemory,
    SrcClockDomain,
    VoltageDomain,
)

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
)

from gw100.boards import BOARDS, DEFAULT_BOARD  # noqa: E402
from gw100.devices import DEFAULT_DEVICE, DEVICES  # noqa: E402
from gw100.hosts import DEFAULT_HOST, HOSTS  # noqa: E402

# 默认路径：仓库根目录下 build/ 中的构建产物
REPO = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
)
DEFAULT_ROM = os.path.join(REPO, "build", "gw100-firmware", "rom.elf")
DEFAULT_RV32_DIR = os.path.join(REPO, "build", "gw100-feasibility")
# 与 make test 的 tick 上限相同（3 s 仿真时间）
DEFAULT_MAX_TICKS = 3_000_000_000_000

APP_EXIT = "semi:ADP_Stopped_ApplicationExit"
# RV32 核的退出子码：基值、失败位、核号的掩码
RV32_EXIT_BASE = 0x100
RV32_EXIT_FAIL = 0x80
RV32_CORE_MASK = 0x7F
EXIT_TICK_LIMIT = 124

# RV32 核的可行性参数（不是产品参数）
RV32_FREQ = "1GHz"
RV32_MEM_SIZE = "64KiB"


class Args(argparse.Namespace):
    """命令行参数；字段的类型供静态检查使用。"""

    board: str
    host: str
    device: str
    kernel: str
    rom: str
    rv32_dir: str
    rv32_cores: int
    max_ticks: int


def make_rv32_system(elf: str) -> RiscvSystem:
    """创建一个 RV32 核的独立系统，运行 elf；存储只属于这个核。"""
    s = RiscvSystem()
    s.voltage_domain = VoltageDomain()
    s.clk_domain = SrcClockDomain(
        clock=RV32_FREQ, voltage_domain=s.voltage_domain
    )
    s.mem_mode = "timing"
    s.mem_ranges = [AddrRange(0x0, size=RV32_MEM_SIZE)]
    s.membus = NoncoherentXBar(
        width=8, frontend_latency=1, forward_latency=0, response_latency=1
    )
    s.system_port = s.membus.cpu_side_ports

    s.cpu = RiscvTimingSimpleCPU()
    s.cpu.isa = [RiscvISA(riscv_type="RV32", enable_rvv=False,
                          privilege_mode_set="M")]
    s.cpu.createThreads()
    s.cpu.createInterruptController()
    s.cpu.connectBus(s.membus)

    s.mem = SimpleMemory(range=s.mem_ranges[0])
    s.mem.port = s.membus.mem_side_ports

    s.workload = RiscvBareMetal(
        bootloader=elf, semihosting=RiscvSemihosting()
    )
    return s


def read_insts(stats: str, names: list[str]) -> dict[str, int]:
    """从 stats.txt 的第一次转储中读出各系统 CPU 的提交指令数。"""
    insts: dict[str, int] = {}
    pat = re.compile(r"^(\S+)\.cpu\.commitStats0\.numInsts\s+(\d+)")
    with open(stats, encoding="utf-8") as f:
        for ln in f:
            if ln.startswith("---------- End Simulation Statistics"):
                break
            m = pat.match(ln)
            if m and m.group(1) in names:
                insts[m.group(1)] = int(m.group(2))
    return insts


def main() -> None:
    parser = argparse.ArgumentParser(
        description="多 ISA 可行性测试：ARM host + RV64 管理核 + RV32 核"
    )
    _ = parser.add_argument("--board", choices=sorted(BOARDS),
                            default=DEFAULT_BOARD)
    _ = parser.add_argument("--host", choices=sorted(HOSTS),
                            default=DEFAULT_HOST)
    _ = parser.add_argument("--device", choices=sorted(DEVICES),
                            default=DEFAULT_DEVICE)
    _ = parser.add_argument("--kernel", required=True,
                            help="ARM host 的裸机 ELF（推荐 boot_ok.elf）")
    _ = parser.add_argument("--rom", default=DEFAULT_ROM,
                            help=f"管理核的启动 ROM（默认 {DEFAULT_ROM}）")
    _ = parser.add_argument(
        "--rv32-dir", default=DEFAULT_RV32_DIR,
        help=f"rv32_hello_<k>.elf 所在目录（默认 {DEFAULT_RV32_DIR}）",
    )
    _ = parser.add_argument("--rv32-cores", type=int, default=16,
                            help="RV32 核的个数（默认 16，最多 128）")
    _ = parser.add_argument(
        "--max-ticks", type=int, default=DEFAULT_MAX_TICKS,
        help=f"仿真 tick 上限（默认 {DEFAULT_MAX_TICKS}）",
    )
    args = parser.parse_args(namespace=Args())

    if not 1 <= args.rv32_cores <= RV32_CORE_MASK + 1:
        parser.error("--rv32-cores 须在 1 – 128 之间")
    elfs = [os.path.join(args.rv32_dir, f"rv32_hello_{k}.elf")
            for k in range(args.rv32_cores)]
    for path in [args.kernel, args.rom] + elfs:
        if not os.path.isfile(path):
            parser.error(f"文件不存在：{path}")

    board_cls = BOARDS[args.board]
    device = DEVICES[args.device]
    dev = device.make()
    board = board_cls(HOSTS[args.host], [dev], bare_metal=True)
    board.set_baremetal_workload(args.kernel)
    board._pre_instantiate()
    root = Root.getInstance()
    root.gw100_mgmt = device.make_mgmt(dev, args.rom)
    names = [f"rv32_{k:02d}" for k in range(args.rv32_cores)]
    for name, elf in zip(names, elfs):
        setattr(root, name, make_rv32_system(elf))

    m5.instantiate()
    board._post_instantiate()

    rv32: dict[int, tuple[bool, int]] = {}
    arm: tuple[int, int] | None = None
    errors: list[str] = []
    tick_limit = False
    while True:
        event = m5.simulate(args.max_ticks - m5.curTick())
        cause, code, tick = event.getCause(), event.getCode(), m5.curTick()
        if cause != APP_EXIT:
            tick_limit = m5.curTick() >= args.max_ticks
            if not tick_limit:
                errors.append(f"意外的退出：{cause}（code {code}）")
            break
        if code & ~(RV32_EXIT_FAIL | RV32_CORE_MASK) == RV32_EXIT_BASE:
            k = code & RV32_CORE_MASK
            if k >= args.rv32_cores or k in rv32:
                errors.append(f"RV32 退出子码 {code:#x} 无效或重复")
                break
            rv32[k] = (not code & RV32_EXIT_FAIL, tick)
        elif arm is None:
            arm = (code, tick)
        else:
            errors.append(f"ARM 程序重复退出（code {code}）")
            break
        if arm is not None and (arm[0] != 0 or len(rv32) == args.rv32_cores):
            break

    m5.stats.dump()
    insts = read_insts(os.path.join(m5.options.outdir, "stats.txt"),
                       ["gw100_mgmt"] + names)

    print(f"[feasibility] host={args.host} tick={m5.curTick()}")
    ok = not errors and not tick_limit
    if arm is None:
        print("[feasibility] arm   : 未退出")
        ok = False
    else:
        res = "PASS" if arm[0] == 0 else f"FAIL（code {arm[0]}）"
        print(f"[feasibility] arm   : {res} at tick {arm[1]}"
              f"（{os.path.basename(args.kernel)}）")
        ok = ok and arm[0] == 0
    mgmt = insts.get("gw100_mgmt", 0)
    print(f"[feasibility] rv64  : insts {mgmt}"
          "（启动 ROM 与固件；ARM 程序通过即证明固件已就绪）")
    ok = ok and mgmt > 0
    for k, name in enumerate(names):
        n = insts.get(name, 0)
        if k in rv32:
            passed, tick = rv32[k]
            res = "PASS" if passed else "FAIL"
            print(f"[feasibility] {name}: {res} at tick {tick} insts {n}")
            ok = ok and passed and n > 0
        else:
            print(f"[feasibility] {name}: 未退出 insts {n}")
            ok = False
    for e in errors:
        print(f"[feasibility] 错误：{e}")
    if tick_limit:
        print(f"[feasibility] 到达 tick 上限 {args.max_ticks}")
    print(f"[feasibility] 结果：{'PASS' if ok else 'FAIL'}"
          f"（1 个 ARM、1 个 RV64、{args.rv32_cores} 个 RV32）")
    sys.exit(EXIT_TICK_LIMIT if tick_limit else (0 if ok else 1))


if __name__ == "__m5_main__":
    main()
