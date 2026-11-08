"""
@file  pcie_checkpoint_fixture.py
@brief 用真实 Host 端口和原生 EP 编解码验证链路子系统 checkpoint

夹具只包含 Host 与 PCIe 子系统。4 KiB 设备后端不承载测量流量。
完整管理 CPU、固件和八通道 DRAM 在正常 gw100 回归中独立验证。
@see spec-pcie
"""

import argparse
import hashlib
import json
import sys
from dataclasses import replace
from pathlib import Path

import m5
from m5.params import NULL

from gw100.boards import BOARDS
from gw100.devices import DEVICES
from gw100.hosts import HOSTS


def main():
    """三个独立进程依次创建 L0、恢复 L0 创建 L1、恢复 L1。"""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[2])
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--host", choices=("timing", "o3"), default="timing")
    parser.add_argument("--device", choices=("gw100",), default="gw100")
    parser.add_argument("--max-ticks", type=int, default=100000000000)
    parser.add_argument("--pcie-rc-aspm", action="store_true")
    parser.add_argument("--checkpoint-dir", type=Path)
    parser.add_argument("--restore", type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    source_files = [
        *sorted((repo / "gem5-ext/src/gw100/pcie").rglob("*.cc")),
        *sorted((repo / "gem5-ext/src/gw100/pcie").rglob("*.hh")),
        repo / "gem5/src/sim/serialize.hh",
        repo / "spec-src/pcie/gw100-link.json",
        args.kernel,
        Path(__file__),
        Path(sys.executable),
    ]
    fingerprints = {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in source_files
    }
    m5.ticks.setGlobalFrequency("1ps")
    base = DEVICES["gw100_compat"]
    config = replace(base, devmem=replace(base.devmem, size="4KiB"))
    device = config.make()
    device.mgmt_link = NULL
    device.precise_link = True
    device.pcie_rc_aspm = True
    board = BOARDS["arm_vexpress"](HOSTS[args.host], [device], bare_metal=True)
    board.set_baremetal_workload(str(args.kernel))
    board._pre_instantiate()
    m5.instantiate(str(args.restore) if args.restore else None)
    board._post_instantiate()
    event = m5.simulate(max(0, args.max_ticks - m5.curTick()))
    checkpoint = None
    if event.getCause() == "semi:ADP_Stopped_UserInterruption":
        if not args.checkpoint_dir:
            raise RuntimeError("checkpoint 标记需要输出目录")
        checkpoint = args.checkpoint_dir.resolve() / str(event.getCode())
        m5.checkpoint(str(checkpoint))
    report = json.loads(device.pcieReport())
    executable = Path(sys.executable)
    built = json.loads(executable.with_suffix(".pcie-build.json").read_text())
    report.update(
        fixture="native-pcie-subsystem",
        capture_tick=m5.curTick(),
        checkpoint=str(checkpoint) if checkpoint else None,
        trace=False,
        fingerprints=fingerprints,
        build=built,
        inputs_unchanged_during_run=all(
            hashlib.sha256(path.read_bytes()).hexdigest()
            == fingerprints[str(path)]
            for path in source_files
        ),
        build_matches_inputs=(
            built["executable_sha256"] == fingerprints[str(executable)]
            and all(
                fingerprints.get(str(repo / name)) == value
                for name, value in built["sources"].items()
            )
        ),
    )
    (Path(m5.options.outdir) / "pcie-report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    )
    code = 0 if checkpoint else event.getCode()
    if (
        not checkpoint
        and event.getCause() != "semi:ADP_Stopped_ApplicationExit"
    ):
        code = 125
    print(f"PCIe fixture exit {event.getCause()} (code {code})")
    sys.exit(code)


if __name__ == "__m5_main__":
    main()
