"""
@file  memory_verify.py
@brief Memory Tile 组件、独立时序计算和真实固件故障矩阵验证

运行前执行 make gem-build baremetal-build。每次仿真使用独立输出目录，
checkpoint 创建与恢复顺序执行，固件覆盖全部通道和 rank。
@see spec-dram
"""

import argparse
import concurrent.futures
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]
GEM5 = ROOT / "gem5/build/GW100/gem5.opt"
COMPONENT = ROOT / "gem5-ext/configs/feasibility/memory_tile.py"
PROFILE = ROOT / "gem5-ext/configs/feasibility/lpddr5_8433.py"
SYSTEM = ROOT / "gem5-ext/configs/run_gw100.py"


def stats(path):
    """读取 gem5 的最后一个统计窗口。"""
    result = {}
    for line in path.read_text().splitlines():
        fields = line.split()
        if len(fields) >= 2:
            try:
                result[fields[0]] = float(fields[1])
            except ValueError:
                pass
    return result


def latency_reference(outdir):
    """独立计算行命中、控制器流水和两级请求同步的精确 tick。"""
    ck = round(Fraction(8_000_000, 8433))
    burst = round(Fraction(6 * 8_000_000, 8433))
    array = 23_280 + burst
    native = array + 4 * ck
    measured = stats(outdir / "stats.txt")
    assert measured["system.controllers0.dram.readRowHits"] == 1
    assert measured["system.controllers0.dram.avgMemAccLat"] == array
    trace = (outdir / "memory.trace").read_text()
    issue = int(
        re.findall(
            r"^(\d+): system.tiles0: media-issue ReadReq 0x200000",
            trace,
            re.MULTILINE,
        )[-1]
    )
    response = int(
        re.findall(
            r"^(\d+): system.tiles0: media-response ReadResp 0x200000",
            trace,
            re.MULTILINE,
        )[-1]
    )
    request = int(
        re.findall(
            r"^(\d+): system.tiles0.channel0: 1000 request 0x200000 beat 1/1",
            trace,
            re.MULTILINE,
        )[-1]
    )
    assert response - issue == native
    assert issue == (request // ck + 2) * ck
    accepted = int(
        re.findall(
            r"^(\d+): system.ingresses0: accept ReadReq 0x200000 64",
            trace,
            re.MULTILINE,
        )[-1]
    )
    completed = int(
        re.findall(
            r"^(\d+): system.ingresses0: complete 0x200000",
            trace,
            re.MULTILINE,
        )[-1]
    )
    return {
        "ck_ps": ck,
        "dram_ps": array,
        "controller_roundtrip_ps": native,
        "request_cdc_ps": issue - request,
        "source_roundtrip_ps": completed - accepted,
        "adapter_roundtrip_ps": completed - accepted - native,
        "raw_channel_peak_MB_s": float(Fraction(8433 * 16, 8)),
        "raw_eight_channel_peak_MB_s": float(Fraction(8433 * 16 * 8, 8)),
    }


def main():
    """执行有界并行验证，失败时保留日志并以非零退出。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--suite", choices=("component", "firmware", "all"), default="all"
    )
    parser.add_argument(
        "--outdir", type=Path, default=ROOT / "build/memory-validation"
    )
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs 需要为正整数")
    args.outdir = args.outdir.resolve()
    args.outdir.mkdir(parents=True, exist_ok=True)
    records = []
    sources = []
    for folder in (
        "gem5-ext/src/gw100",
        "gem5-ext/src/memory",
        "firmware/fw",
        "firmware/rom",
    ):
        sources.extend(
            p
            for p in (ROOT / folder).rglob("*")
            if p.suffix in (".cc", ".hh", ".py", ".c", ".h", ".S")
        )
    sources.extend((ROOT / "firmware/common").glob("*.[ch]"))
    for folder in ("devices", "boards", "hosts"):
        sources.extend((ROOT / "gem5-ext/configs/gw100" / folder).glob("*.py"))
    native_paths = subprocess.check_output(
        [
            "git", "-C", str(ROOT), "ls-files", "--cached", "--others",
            "--exclude-standard", "-z", "--", "gem5",
        ]
    )
    sources.extend(
        ROOT / path.decode()
        for path in native_paths.split(b"\0")
        if path and (ROOT / path.decode()).is_file()
    )
    sources.extend(
        ROOT / name
        for name in (
            "gem5-ext/configs/run_gw100.py",
            "gem5-ext/configs/gw100/generated/memory_defs.py",
            "spec-src/dram/gw100-memory.json",
            "firmware/common/generated/gw100_memory_defs.h",
        )
    )
    digest = hashlib.sha256()
    for path in sorted(sources):
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(path.read_bytes())
    metadata = {
        "model_version": "gw100-memory-v0.2",
        "input_sha256": digest.hexdigest(),
        "binary_sha256": hashlib.sha256(GEM5.read_bytes()).hexdigest(),
        "seed": 5489,
        "gem5_revision": subprocess.check_output(
            ["git", "-C", str(ROOT / "gem5"), "rev-parse", "HEAD"],
            text=True,
        ).strip(),
    }

    def run(name, config, arguments, trace=False):
        """运行一个独立仿真并保存原始命令、退出码和耗时。"""
        out = args.outdir / name
        out.mkdir(parents=True, exist_ok=True)
        cmd = [str(GEM5), "--outdir=" + str(out)]
        if trace:
            cmd += ["--debug-flags=Gw100Memory", "--debug-file=memory.trace"]
        cmd += [str(config), *arguments]
        started = time.monotonic()
        with (out / "run.log").open("w") as log:
            log.write("$ " + " ".join(cmd) + "\n")
            log.flush()
            process = subprocess.run(
                cmd,
                cwd=ROOT,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=900,
            )
        record = {
            **metadata,
            "name": name,
            "command": cmd,
            "exit_code": process.returncode,
            "wall_seconds": time.monotonic() - started,
            "measurement_ticks": int(stats(out / "stats.txt")["simTicks"]),
        }
        records.append(record)
        if process.returncode:
            raise RuntimeError(f"{name} 失败，见 {out / 'run.log'}")
        print(f"PASS {name}", flush=True)
        return out

    if args.suite in ("component", "all"):
        for mode in ("single", "stress", "reset", "retention", "latency"):
            out = run(mode, COMPONENT, ["--mode", mode], mode == "latency")
            report = json.loads((out / "memory-probe.json").read_text())
            assert report["bytes"] == 32 << 30
            if mode in ("stress", "reset"):
                assert report["source_retries"] > 0
            if mode == "latency":
                records[-1]["reference"] = latency_reference(out)
        for phase in (2, 3):
            for rank in (0, 15):
                for fault in ("fail", "timeout"):
                    run(
                        f"component-{phase}-{rank}-{fault}",
                        COMPONENT,
                        [
                            "--mode",
                            "fault",
                            "--fault-phase",
                            str(phase),
                            f"--{fault}-mask",
                            str(1 << rank),
                        ],
                    )
        for mode in ("latency", "read", "write", "mixed"):
            run("profile-" + mode, PROFILE, ["--mode", mode])
        for state in ("ready", "self-refresh"):
            arguments = ["--mode", "checkpoint", "--checkpoint-state", state]
            saved = run("checkpoint-" + state, COMPONENT, arguments)
            run(
                "restore-" + state,
                COMPONENT,
                arguments + ["--restore", str(saved / "checkpoint")],
            )

    if args.suite in ("firmware", "all"):
        kernel = ROOT / "build/gw100-baremetal/dram_error.elf"
        common = [
            "--host",
            "timing",
            "--device",
            "gw100",
            "--kernel",
            str(kernel),
            "--max-ticks",
            "3000000000000",
        ]

        def firmware_fault(case):
            """检查固件实际报告的通道、rank、阶段与错误原因。"""
            phase, rank, fault = case
            name = f"firmware-{phase}-{rank}-{fault}"
            out = run(
                name,
                SYSTEM,
                common
                + [
                    "--dram-fault-phase",
                    phase,
                    f"--dram-{fault}-mask",
                    str(1 << rank),
                ],
            )
            text = (out / "run.log").read_text()
            diagnostic = int(re.search(r"快照 0x([0-9a-fA-F]+)", text)[1], 16)
            reason = 6 if fault == "fail" else 7
            stage = 2 if phase == "calibrate" else 3
            expected = (
                0xD2000000
                | (rank // 4) << 20
                | (rank // 2 % 2) << 18
                | (rank % 2) << 16
                | stage << 8
                | reason
            )
            assert diagnostic == expected, (
                name,
                hex(diagnostic),
                hex(expected),
            )

        cases = [
            (phase, rank, fault)
            for phase in ("calibrate", "train")
            for rank in range(16)
            for fault in ("fail", "timeout")
        ]
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            list(pool.map(firmware_fault, cases))

        def firmware_selftest(rank):
            """逐 rank 注入自检读取错误并验证固件位置快照。"""
            out = run(
                f"firmware-selftest-{rank}",
                SYSTEM,
                common + ["--dram-corrupt-mask", str(1 << rank)],
            )
            text = (out / "run.log").read_text()
            diagnostic = int(re.search(r"快照 0x([0-9a-fA-F]+)", text)[1], 16)
            expected = (
                0xD200080A
                | (rank // 4) << 20
                | (rank // 2 % 2) << 18
                | (rank % 2) << 16
            )
            assert diagnostic == expected, (
                rank,
                hex(diagnostic),
                hex(expected),
            )

        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            list(pool.map(firmware_selftest, range(16)))
        out = run(
            "firmware-selftest-switch",
            SYSTEM,
            common + ["--dram-corrupt-selftest"],
        )
        assert "0xd200080a" in (out / "run.log").read_text().lower()

    (args.outdir / "results.json").write_text(
        json.dumps(sorted(records, key=lambda r: r["name"]), indent=2) + "\n"
    )
    print(f"{len(records)} 项验证通过，记录 {args.outdir / 'results.json'}")


if __name__ == "__main__":
    main()
