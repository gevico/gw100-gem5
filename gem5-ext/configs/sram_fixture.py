"""
@file  sram_fixture.py
@brief 共享 SRAM 的管理与 Compute 档案验证入口
@see spec-sram
"""

import argparse
import sys
from pathlib import Path

import m5
from m5.objects import (
    Gw100Sram,
    Root,
    SramProbe,
    SrcClockDomain,
    System,
    VoltageDomain,
)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gw100.generated import sram_defs as defs

parser = argparse.ArgumentParser()
parser.add_argument(
    "--profile",
    choices=["management", "compute_1rw", "compute_1r1w"],
    default="compute_1rw",
)
parser.add_argument("--latency", type=int, default=1)
parser.add_argument("--interval", type=int, default=1)
parser.add_argument("--checkpoint")
parser.add_argument("--restore")
parser.add_argument("--retention", action="store_true")
parser.add_argument("--trace", default="")
parser.add_argument("--reset", action="store_true")
parser.add_argument("--clock-switch", action="store_true")
args = parser.parse_args()
prefix = args.profile.upper() + "_"
value = lambda key: getattr(defs, prefix + key)
s = System()
s.clk_domain = SrcClockDomain(
    clock=f"{value('PERIOD_PS')}ps", voltage_domain=VoltageDomain()
)
if args.clock_switch:
    s.clk_domain = SrcClockDomain(
        clock=[f"{value('PERIOD_PS')}ps", "40000ps"],
        init_perf_level=1,
        voltage_domain=VoltageDomain(),
    )
s.mem_mode = "timing"
s.sram = Gw100Sram(
    size=f"{value('SIZE')}B",
    banks=value("BANKS"),
    word_bytes=value("WORD_BYTES"),
    sources=value("SOURCES"),
    slots=value("SLOTS"),
    mask_bytes=value("MASK_BYTES"),
    interleaved=bool(value("INTERLEAVED")),
    split_ports=bool(value("SPLIT_PORTS")),
    read_cycles=args.latency,
    write_cycles=args.latency,
    interval_cycles=args.interval,
    trace_file=args.trace,
)
s.probe = SramProbe(
    sram=s.sram,
    snapshot=bool(args.checkpoint),
    retention=args.retention,
    reset_test=args.reset,
    clock_switch=args.clock_switch,
)
for source in range(value("SOURCES")):
    s.probe.ports = s.sram.ports
root = Root(full_system=False, system=s)
m5.ticks.setGlobalFrequency("1ps")
m5.instantiate(args.restore)
event = m5.simulate(10_000_000_000)
if event.getCause() == "sram checkpoint":
    if not args.checkpoint:
        raise SystemExit("意外 checkpoint 请求")
    m5.checkpoint(args.checkpoint)
    event = m5.simulate(10_000_000_000)
if event.getCause() != "sram pass":
    raise SystemExit(f"SRAM 夹具失败 {event.getCause()}")
