"""
@file  sram_verify.py
@brief SRAM 独立数据、时序、资源与性能验收
@see spec-sram
"""

import argparse
import hashlib
import json
import os
import random
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/sram"
MODEL = ROOT / "gem5-ext/src/gw100/sram/sram_model.cc"
PROBE = ROOT / "gem5-ext/tests/integration/sram/sram_reference_probe.cc"


def sha(path):
    """对输入文件取指纹。"""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build():
    """驱动器和生产模型一起构建，期望由本工具独立计算。"""
    BUILD.mkdir(parents=True, exist_ok=True)
    executable = BUILD / "reference-probe"
    subprocess.run(
        [
            os.environ.get("CXX", "c++"),
            "-std=c++17",
            "-O2",
            "-I",
            str(ROOT / "gem5-ext"),
            str(MODEL),
            str(PROBE),
            "-o",
            str(executable),
        ],
        check=True,
    )
    return executable


def run(executable, commands="", args=()):
    """保存完整输出，返回结构化事件。"""
    result = subprocess.run(
        [str(executable), *args],
        input=commands,
        text=True,
        capture_output=True,
        check=True,
    )
    return [json.loads(line) for line in result.stdout.splitlines()]


def timing(executable):
    """用独立的边界公式检查发出、提交、采样和响应就绪。"""
    cases = []
    for latency in (1, 2, 3):
        records = run(
            executable,
            "W 0 0 8 255 19\nS 8\nT 0\n" "R 0 0 8\nS 8\nT 0\n",
            ("--trace", "--latency", str(latency)),
        )
        issue = {}
        ready = {}
        for event in records:
            if event.get("event") in ("issue_write", "issue_read"):
                issue[event["id"]] = event["cycle"]
            if event.get("event") in ("commit", "ready"):
                assert event["cycle"] == issue[event["id"]] + latency, event
            if event.get("event") == "sample":
                assert event["cycle"] == issue[event["id"]], event
            if event.get("event") == "ready":
                ready[event["id"]] = event["cycle"]
        results = [event for event in records if "result" in event]
        assert len(results) == 2 and all(r["status"] == 0 for r in results)
        expected = bytes(range(19, 27))
        assert bytes.fromhex(results[1]["data"])[:8] == expected
        assert len(ready) == 2
        cases.append({"latency": latency, "event_error_ticks": 0})
    return cases


def data_and_resources(executable):
    """期望数据由 Python 字节数组计算，覆盖跨字、掩码与响应保留。"""
    records = run(
        executable,
        "W 0 7 64 18446744073709551615 97\n"
        "S 40\nT 0\nR 0 0 64\nS 40\nT 0\n"
        "W 1 15 2 1 201\nS 8\nT 1\nR 0 0 64\nS 40\nT 0\n",
    )
    results = [item for item in records if "result" in item]
    expected = bytearray(128)
    expected[7:71] = bytes(range(97, 161))
    assert bytes.fromhex(results[1]["data"]) == expected[:64]
    expected[15] = 201
    assert bytes.fromhex(results[3]["data"]) == expected[:64]
    records = run(
        executable,
        "W 0 0 8 255 1\nS 1\n"
        "W 0 8 8 255 2\nS 8\nW 0 16 8 255 3\n"
        "T 0\nW 0 16 8 255 3\n",
        ("--slots", "2"),
    )
    submissions = [r for r in records if "submit" in r]
    assert [r["accepted"] for r in submissions] == [True, True, False, True]
    return {"byte_array": "pass", "blocked_response_reservation": "pass"}


def ecc(executable):
    """独立校验矩阵与故障向量，避免两个 C++ 函数共用错误位序。"""
    positions = [p for p in range(1, 72) if p & (p - 1)]
    matrix = [
        sum(1 << bit for bit, p in enumerate(positions) if p & (1 << row))
        for row in range(7)
    ]
    rng = random.Random(0)
    values = [0, 2**64 - 1, 0x0123456789ABCDEF]
    values += [1 << bit for bit in range(64)]
    values += [rng.getrandbits(64) for _ in range(32)]
    commands, expected = [], []
    for data in values:
        check = sum(
            ((data & row).bit_count() % 2) << bit
            for bit, row in enumerate(matrix)
        )
        check |= ((data.bit_count() + check.bit_count()) % 2) << 7
        commands.append(f"E {data}")
        expected.append({"ecc": check})
        for bit in range(72):
            corrupted = data ^ (1 << bit) if bit < 64 else data
            parity = check ^ (1 << (bit - 64)) if bit >= 64 else check
            commands.append(f"D {corrupted} {parity}")
            expected.append({"data": data, "ce": 1, "ue": 0})
    assert run(executable, "\n".join(commands) + "\n") == expected
    return {
        "matrix_vectors": len(values),
        "single_bit_vectors": len(values) * 72,
    }


def performance(executable):
    """从端口数量、II 与每次数据量推导饱和参考。"""
    cases = []
    for compute, split in ((False, False), (True, False), (True, True)):
        word, sources, period = (16, 5, 1000) if compute else (8, 3, 2000)
        for latency, interval in (
            (1, 1),
            (2, 1),
            (3, 1),
            (1, 2),
            (2, 2),
            (3, 2),
        ):
            for kind in ("read", "hotspot", "mixed", "partial"):
                if kind == "partial" and (latency, interval) != (1, 1):
                    continue
                args = [
                    "--bench",
                    kind,
                    "--period",
                    str(period),
                    "--latency",
                    str(latency),
                    "--interval",
                    str(interval),
                ]
                if compute:
                    args.append("--compute")
                if split:
                    args.append("--split")
                actual = run(executable, args=args)[0]
                if kind == "read":
                    payload_per_cycle = sources * word
                elif kind == "partial":
                    payload_per_cycle = 1 if split else 0.5
                else:
                    payload_per_cycle = (
                        word
                        * (2 if split and kind == "mixed" else 1)
                        / interval
                    )
                expected = payload_per_cycle * actual["window_ps"] / period
                deviation = (actual["bytes"] - expected) / expected
                assert abs(deviation) <= 0.01, (args, actual, expected)
                assert actual["peak_slots"] <= sources * 8
                if not compute and latency == interval == 1:
                    assert actual["max_bank_wait"] <= 16
                cases.append(
                    {
                        "profile": (
                            "compute_1r1w"
                            if split
                            else "compute_1rw" if compute else "management"
                        ),
                        "kind": kind,
                        "latency": latency,
                        "interval": interval,
                        "expected_bytes": expected,
                        "deviation": deviation,
                        **actual,
                    }
                )
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", type=Path, default=BUILD / "independent.json"
    )
    args = parser.parse_args()
    executable = build()
    report = {
        "timing": timing(executable),
        "data": data_and_resources(executable),
        "ecc": ecc(executable),
        "performance": performance(executable),
        "fingerprints": {
            str(p.relative_to(ROOT)): sha(p)
            for p in (
                MODEL,
                PROBE,
                Path(__file__).resolve(),
                ROOT / "gem5-ext/src/gw100/sram/sram_model.hh",
                ROOT / "gem5-ext/src/gw100/sram/generated/sram_defs.hh",
                ROOT / "spec-src/sram/gw100-sram.json",
                executable,
            )
        },
        "warmup_ps": 10_000_000,
        "window_ps": 100_000_000,
        "seed": 0,
        "basis": "通用数字参考档案",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    )
    print(f"SRAM 独立检查 PASS，{len(report['performance'])} 个性能场景")


if __name__ == "__main__":
    main()
