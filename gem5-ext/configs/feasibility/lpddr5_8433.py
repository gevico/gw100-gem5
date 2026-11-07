"""
@file  lpddr5_8433.py
@brief 通用 LPDDR5-8433 的 timing 流量与手算对照

latency 在首次刷新前测量预热后的单行读取。read、write、mixed 使用
默认页面策略和刷新，检查实际数据流量及峰值带宽约束。在途请求有限，
结束时排空控制器，结果由正常 gem5 统计输出。

@see spec-dram
"""

import argparse
import math
from pathlib import Path

import m5
from m5.objects import (
    AddrRange,
    IOXBar,
    LPDDR5_8433_1x16_BG_BL32,
    PyTrafficGen,
    Root,
    SrcClockDomain,
    System,
    VoltageDomain,
)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--mode", choices=("latency", "read", "write", "mixed"), default="latency"
)
args = parser.parse_args()

m5.ticks.setGlobalFrequency("1ps")
system = System(mem_mode="timing", cache_line_size=64)
system.clk_domain = SrcClockDomain(
    clock="2GHz", voltage_domain=VoltageDomain()
)
system.mem_ranges = [AddrRange("1GiB")]
system.mmap_using_noreserve = True
system.membus = IOXBar(width=32)
system.mem_ctrl = LPDDR5_8433_1x16_BG_BL32().controller()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports
system.tgen = PyTrafficGen(max_outstanding_reqs=64)
system.tgen.port = system.membus.cpu_side_ports
system.system_port = system.membus.cpu_side_ports

# 稀疏请求将一个已打开的行保持到首次刷新，隔离排队和刷新开销。
if args.mode == "latency":
    system.mem_ctrl.dram.page_policy = "open"

root = Root(full_system=False, system=system)
m5.instantiate()


def traffic():
    """生成有界读写流量，并以正常仿真退出事件结束。"""
    if args.mode == "latency":
        # 400 ns 预热加 2000 ns 测量，少于一次刷新间隔和行访问上限。
        yield system.tgen.createLinear(
            2_400_000, 0, 64, 64, 200_000, 200_000, 100, 0
        )
    else:
        read_percent = {"read": 100, "write": 0, "mixed": 50}[args.mode]
        yield system.tgen.createLinear(
            100_000_000, 0, 1 << 20, 64, 500, 500, read_percent, 0
        )
    yield system.tgen.createExit(0)


system.tgen.start(traffic())
if args.mode == "latency":
    warmup = m5.simulate(400_000)
    if warmup.getCause() != "simulate() limit reached":
        raise RuntimeError(f"预热提前结束 {warmup.getCause()}")
    m5.stats.reset()

event = m5.simulate()
expected_exit = (
    f"{system.tgen.path()} has encountered the exit state and will "
    "terminate the simulation."
)
if event.getCause().strip() != expected_exit:
    raise RuntimeError(f"流量未完成 {event.getCause()}")
m5.drain()
m5.stats.dump()

# 使用独立的引脚位宽/数据率手算对照内核的统计。
stats = {}
for line in (Path(m5.options.outdir) / "stats.txt").read_text().splitlines():
    fields = line.split()
    if len(fields) >= 2 and fields[0].startswith("system."):
        try:
            stats[fields[0]] = float(fields[1])
        except ValueError:
            pass

prefix = "system.mem_ctrl.dram."
expected_peak_mb_s = 8433 * 16 / 8
peak_mb_s = stats[prefix + "peakBW"]
if not math.isclose(peak_mb_s, expected_peak_mb_s, rel_tol=0.05):
    raise RuntimeError(f"峰值带宽与手算不符 {peak_mb_s} MB/s")

read_bytes = stats[prefix + "dramBytesRead"]
write_bytes = stats[prefix + "dramBytesWritten"]
if args.mode in ("latency", "read", "mixed") and read_bytes <= 0:
    raise RuntimeError("缺少实际 DRAM 读流量")
if args.mode in ("write", "mixed") and write_bytes <= 0:
    raise RuntimeError("缺少实际 DRAM 写流量")

observed_mb_s = stats[prefix + "avgRdBW"] + stats[prefix + "avgWrBW"]
if not 0 < observed_mb_s <= expected_peak_mb_s * 1.05:
    raise RuntimeError(f"实测流量越过带宽上限 {observed_mb_s} MB/s")

if args.mode == "latency":
    # 基类 tCL 为 23.28 ns，BL32 的完整跨度为六个 CK。
    expected_ns = 23.28 + 6 * 8000 / 8433
    measured_ns = stats[prefix + "avgMemAccLat"] / 1000
    if stats[prefix + "readRowHitRate"] != 100:
        raise RuntimeError("延迟测量窗口中出现行未命中")
    if not math.isclose(measured_ns, expected_ns, abs_tol=0.001):
        raise RuntimeError(
            f"行命中服务时间 {measured_ns} ns，手算 {expected_ns} ns"
        )
    print(f"行命中服务时间 {measured_ns:.3f} ns，手算 {expected_ns:.6f} ns")

ck_ticks = system.mem_ctrl.dram.tCK.getValue()
print(f"CK 周期 {ck_ticks} ps，等效数据率 {8_000_000 / ck_ticks:.3f} MT/s")
print(
    f"峰值 {peak_mb_s:.2f} MB/s，手算 {expected_peak_mb_s:.2f} MB/s，"
    f"实测读写流量 {observed_mb_s:.2f} MB/s"
)
print(f"LPDDR5-8433 {args.mode} PASS")
