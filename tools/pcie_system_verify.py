"""
@file  pcie_system_verify.py
@brief 运行十二种链路的软件冒烟、线上故障和实际 checkpoint 恢复

先执行 make all。本工具使用正常 timing/O3 入口、真实裸机镜像和固件。
各进程保存独立日志、实际配置、指纹及统计，任一必测失败使检查失败。
"""

import argparse
import concurrent.futures
import itertools
import json
import pathlib
import subprocess
import sys
import time

from pcie_verify import ROOT, PROFILE, check_trace, digest


def run_case(
    output,
    name,
    kernel,
    host="timing",
    options=(),
    expected=0,
    config="run_gw100.py",
):
    """运行正常软件路径，保留完整命令与有界失败信息。"""
    directory = output / name
    directory.mkdir(parents=True, exist_ok=True)
    executable = ROOT / "gem5/build/GW100/gem5.opt"
    command = [
        str(executable),
        "-q",
        f"--outdir={directory}",
        str(ROOT / f"gem5-ext/configs/{config}"),
        "--host",
        host,
        "--device",
        "gw100",
        "--kernel",
        str(ROOT / f"build/gw100-baremetal/{kernel}.elf"),
        "--max-ticks",
        "3000000000000",
        *map(str, options),
    ]
    start = time.monotonic()
    entry = dict(name=name, command=command, expected_exit=expected)
    try:
        with (directory / "test.log").open("w") as log:
            log.write("$ " + " ".join(command) + "\n")
            log.flush()
            result = subprocess.run(
                command,
                cwd=ROOT / "gem5",
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=900,
            )
        entry["exit"] = result.returncode
        if result.returncode != expected:
            raise ValueError(f"退出码 {result.returncode}，期望 {expected}")
        report = json.loads((directory / "pcie-report.json").read_text())
        if not report.get("inputs_unchanged_during_run"):
            raise ValueError("运行期间源码或镜像发生变化")
        if not report.get("build_matches_inputs"):
            raise ValueError("可执行文件与构建指纹不匹配")
        if expected and not any(report["stats"]["errors"]):
            raise ValueError("失败场景未记录链路诊断")
        entry["report"] = report
        if report["trace"]:
            entry["wire_check"] = check_trace(
                directory / "pcie-link.jsonl",
                report,
                json.loads(PROFILE.read_text()),
                native=True,
            )
        entry["status"] = "pass"
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        entry.update(status="fail", error=str(error))
    entry["wall_seconds"] = time.monotonic() - start
    print(f"{entry['status'].upper()} {name}", flush=True)
    return entry


def smoke_cases():
    """全部链路组合运行启动、COPY 和真正的 ITS MSI-X。"""
    for generation, width in itertools.product(range(1, 5), (1, 2, 4)):
        for kernel in ("boot_ok", "hdi_copy", "msix_its"):
            options = ("--pcie-gen", generation, "--pcie-width", width)
            yield (
                f"gen{generation}x{width}-timing-{kernel}",
                kernel,
                "timing",
                options,
            )
    for generation, width in ((1, 1), (3, 2)):
        for kernel in ("boot_ok", "hdi_copy", "msix_its"):
            options = ("--pcie-gen", generation, "--pcie-width", width)
            yield (
                f"gen{generation}x{width}-o3-{kernel}",
                kernel,
                "o3",
                options,
            )


def fault_cases():
    """七种线上故障在真实枚举期间注入，协议负责恢复。"""
    for fault in (
        "tlp_crc",
        "dllp_crc",
        "drop_tlp",
        "drop_ack",
        "drop_update",
        "framing",
        "duplicate",
    ):
        side = int(fault in ("drop_ack", "drop_update"))
        options = (
            "--pcie-fault",
            fault,
            "--pcie-fault-side",
            side,
            "--pcie-trace",
        )
        yield (f"fault-{fault}", "pcie_config", "timing", options)
    for service in ("250ns", "2500ns"):
        yield (
            f"host-service-{service}",
            "hdi_copy",
            "timing",
            ("--pcie-host-service", service),
        )
    yield (
        "phy-limit-gen1x1",
        "pcie_link",
        "timing",
        ("--pcie-phy-gen", 1, "--pcie-lane-mask", 1),
    )
    for phase in (
        "Detect.Active",
        "Polling.Active",
        "Configuration.Complete",
        "Recovery.Equalization.Phase1",
    ):
        yield (
            f"phy-failure-{phase}",
            "pcie_config",
            "timing",
            ("--pcie-training-fail", phase),
            1,
        )


def checkpoint_cases(output):
    """用三个独立 gem5 进程验证实际 L0 和 L1 文件恢复。"""
    checkpoints = output / "checkpoints"
    common = ("--pcie-rc-aspm",)
    entries = []
    for index, (name, options) in enumerate(
        (
            ("checkpoint-L0", (*common, "--checkpoint-dir", checkpoints)),
            (
                "restore-L0-checkpoint-L1",
                (
                    *common,
                    "--restore",
                    checkpoints / "256",
                    "--checkpoint-dir",
                    checkpoints,
                ),
            ),
            ("restore-L1", (*common, "--restore", checkpoints / "257")),
        )
    ):
        entry = run_case(
            output,
            name,
            "pcie_checkpoint",
            options=options,
            config="pcie_checkpoint_fixture.py",
        )
        if entry["status"] == "pass" and index < 2:
            expected_state = "L0" if index == 0 else "L1"
            if entry["report"]["state"] != expected_state:
                entry.update(status="fail", error="checkpoint 链路状态错误")
        entries.append(entry)
        if entry["status"] != "pass":
            break
    return entries


def main():
    """保存实际执行矩阵，零通过或任一失败均返回非零。"""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[2])
    parser.add_argument(
        "--suite",
        choices=("all", "smoke", "faults", "checkpoint"),
        default="all",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=ROOT / "build/pcie-system-verification",
    )
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs 必须为正")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = []
    if args.suite in ("all", "smoke"):
        cases.extend(smoke_cases())
    if args.suite in ("all", "faults"):
        cases.extend(fault_cases())
    entries = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(run_case, output, *case) for case in cases]
        for future in concurrent.futures.as_completed(futures):
            entries.append(future.result())
    if args.suite in ("all", "checkpoint"):
        entries.extend(checkpoint_cases(output))
    failed = sum(entry["status"] != "pass" for entry in entries)
    report = dict(
        profile_sha256=digest(PROFILE),
        suite=args.suite,
        cases=sorted(entries, key=lambda entry: entry["name"]),
    )
    (output / "results.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    )
    print(f"PCIe 系统验证 {len(entries) - failed}/{len(entries)}")
    return int(not entries or bool(failed))


if __name__ == "__main__":
    sys.exit(main())
