"""
@file  run_op.py
@brief 运行一个算子测试包

测试包由 tools/gw100_testkit/harness.py 生成。

用法：
    gem5.opt [--debug-flags=Gw100Dispatch,Gw100Ctrl,Gw100Unit] gem5-ext/configs/run_op.py <测试包目录>

测试包目录里应有 package.json，内容包括架构参数、加载/读回列表、命令记录地址等。

@see MAN-050
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import m5  # noqa: E402
from chip import build_chip  # noqa: E402
from m5.objects import Gw100TestHost, Root  # noqa: E402

parser = argparse.ArgumentParser(description="运行一个 GW100 算子测试包")
parser.add_argument("package", help="测试包目录")
args = parser.parse_args()

pkg_dir = os.path.abspath(args.package)
with open(os.path.join(pkg_dir, "package.json"), encoding="utf-8") as f:
    pkg = json.load(f)


def rel(path):
    return os.path.join(pkg_dir, path)


system = build_chip(pkg["arch_params"], functional=pkg.get("functional", True))
system.testhost = Gw100TestHost(
    loads=[f"{l['addr']:#x}={rel(l['file'])}" for l in pkg["loads"]],
    dumps=[f"{d['addr']:#x}:{d['bytes']:#x}={rel(d['file'])}" for d in pkg["dumps"]],
    mgmt_base=pkg["arch_params"]["address_map"]["mgmt_mmio_base"],
    cmd_record_addr=pkg["cmd_record_addr"],
    completion_addr=pkg["completion_addr"],
    timeout=pkg.get("timeout", "100ms"),
    result_file=rel("result.json"),
)
system.testhost.port = system.noc.cpu_side_ports

root = Root(full_system=False, system=system)
m5.instantiate()
event = m5.simulate()
print(f"仿真结束于 tick {m5.curTick()}：{event.getCause()}（退出码 {event.getCode()}）")
m5.stats.dump()
sys.exit(event.getCode())
