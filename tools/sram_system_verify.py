"""
@file  sram_system_verify.py
@brief SRAM timing 端口、周期事件与文件恢复矩阵
@see spec-sram
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def trace_check(path, profile, latency, interval):
    """独立核对真实 gem5 tick、物理端口与资源守恒。"""
    compute = profile.startswith("compute")
    banks, word, period = (16, 16, 1000) if compute else (8, 8, 2000)
    split = profile.endswith("1r1w")
    size = 2 << 20
    deadlines, writes, reads, last_port, live = {}, {}, {}, {}, {}
    count = 0
    for line in path.read_text().splitlines():
        event = json.loads(line)
        kind, tx, address, tick = (
            event[k] for k in ("event", "id", "address", "tick")
        )
        key = tx, address
        source = event["source"]
        if kind == "transport":
            assert event["arrival"] >= event["upstream_accept"]
            assert tick >= event["arrival"]
            assert tick - event["arrival"] <= period * 8 * 64, event
        elif kind == "accept":
            live[source] = live.get(source, 0) + 1
            assert live[source] <= 8, event
        elif kind == "response":
            live[source] -= 1
            assert live[source] >= 0, event
        elif kind.startswith("issue_"):
            bank = (
                (address // word) % banks
                if compute
                else address // (size // banks)
            )
            port = bank, kind if split else "shared"
            if port in last_port:
                assert tick - last_port[port] >= period * interval, event
            last_port[port] = tick
            deadlines[tx] = max(deadlines.get(tx, 0), tick + latency * period)
            if kind == "issue_write":
                writes[key] = tick + latency * period
            else:
                reads[key] = tick
        elif kind == "commit":
            assert abs(tick - writes.pop(key)) <= 1, event
            count += 1
        elif kind == "sample":
            assert tick == reads.pop(key), event
            count += 1
        elif kind == "ready":
            assert abs(tick - deadlines[tx]) <= 1, event
            count += 1
    assert not writes and not reads and all(n == 0 for n in live.values())
    assert count > 100
    return count


def clock_check(path):
    """独立检查暂停窗口、两档实际周期和跨切换写入的剩余周期。"""
    events = [json.loads(line) for line in path.read_text().splitlines()]
    pauses = [e for e in events if e["event"] == "pause"]
    resumes = [e for e in events if e["event"] == "resume"]
    assert len(pauses) == len(resumes) == 1
    pause, resume = pauses[0], resumes[0]
    assert resume["tick"] - pause["tick"] == 84000
    assert pause["cycle"] == resume["cycle"]
    service = [
        e
        for e in events
        if e["event"]
        in {"issue_read", "issue_write", "sample", "commit", "ready"}
    ]
    assert not any(
        pause["tick"] <= e["tick"] < resume["tick"] for e in service
    )
    # gem5 更新周期前先沿旧时钟对齐，保留该边沿作为新档案的相位原点。
    first_edge = ((resume["tick"] + 39999) // 40000) * 40000
    for event in service:
        origin, period = (
            (0, 40000) if event["tick"] < pause["tick"] else (first_edge, 2000)
        )
        assert (event["tick"] - origin) % period == 0, event

    def deadline(issue):
        """从实际发出时点计算三个有效周期后的独立期望。"""
        if issue["tick"] >= resume["tick"]:
            return issue["tick"] + 3 * 2000
        before_pause = (pause["tick"] - issue["tick"] - 1) // 40000
        if before_pause >= 3:
            return issue["tick"] + 3 * 40000
        return first_edge + (3 - before_pause - 1) * 2000

    first = min(e["tick"] for e in service if e["tick"] >= resume["tick"])
    assert 0 <= first - resume["tick"] <= 40000
    reads, writes, due, crossing = {}, {}, {}, 0
    for event in events:
        key = event["id"], event["address"]
        if event["event"] == "issue_read":
            reads[key] = event
            due[event["id"]] = max(due.get(event["id"], 0), deadline(event))
        elif event["event"] == "issue_write":
            if key in reads:
                read = reads.pop(key)
                assert event["cycle"] - read["cycle"] == 3
                assert abs(event["tick"] - deadline(read)) <= 1, event
                crossing += read["tick"] < pause["tick"] < event["tick"]
            writes[key] = event
            due[event["id"]] = max(due.get(event["id"], 0), deadline(event))
        elif event["event"] == "commit":
            issue = writes.pop(key)
            assert event["cycle"] - issue["cycle"] == 3
            assert abs(event["tick"] - deadline(issue)) <= 1, event
            crossing += issue["tick"] < pause["tick"] < event["tick"]
        elif event["event"] == "ready":
            assert abs(event["tick"] - due[event["id"]]) <= 1, event
    assert not writes and crossing > 0
    return {
        "pause_tick": pause["tick"],
        "resume_tick": resume["tick"],
        "first_service_after_resume_tick": first,
        "resume_edge_wait_ps": first_edge - resume["tick"],
        "crossing_operations": crossing,
        "event_error_ticks": 0,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--gem5", type=Path, default=ROOT / "gem5/build/GW100/gem5.opt"
    )
    parser.add_argument(
        "--output", type=Path, default=ROOT / "build/sram/system"
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cases = []

    def run(name, options, check=None):
        out = args.output / name
        out.mkdir(parents=True, exist_ok=True)
        command = [
            str(args.gem5),
            "-q",
            f"--outdir={out}",
            str(ROOT / "gem5-ext/configs/sram_fixture.py"),
            *options,
        ]
        log = out / "test.log"
        with log.open("w") as stream:
            result = subprocess.run(
                command,
                stdout=stream,
                stderr=subprocess.STDOUT,
                cwd=ROOT,
                timeout=120,
            )
        text = log.read_text()
        if result.returncode or "SRAM_PROBE_PASS" not in text:
            raise RuntimeError(f"{name} 失败\n{text[-4000:]}")
        case = {
            "name": name,
            "command": command,
            "returncode": result.returncode,
            "log_sha256": fingerprint(log),
        }
        if check:
            trace, profile, latency, interval = check
            case["checked_events"] = trace_check(
                trace, profile, latency, interval
            )
            case["trace_sha256"] = fingerprint(trace)
        cases.append(case)
        print(f"PASS {name}", flush=True)

    for profile in ("management", "compute_1rw", "compute_1r1w"):
        for latency in (1, 2, 3):
            for interval in (1, 2):
                name = f"{profile}-l{latency}-i{interval}"
                trace = args.output / name / "trace.jsonl"
                run(
                    name,
                    [
                        "--profile",
                        profile,
                        "--latency",
                        str(latency),
                        "--interval",
                        str(interval),
                        "--trace",
                        str(trace),
                    ],
                    (trace, profile, latency, interval),
                )
    for retention in (False, True):
        mode = "retention" if retention else "ready"
        checkpoint = args.output / f"checkpoint-{mode}"
        options = ["--profile", "compute_1r1w"]
        if retention:
            options.append("--retention")
        run(f"save-{mode}", [*options, "--checkpoint", str(checkpoint)])
        run(f"restore-{mode}", [*options, "--restore", str(checkpoint)])
    for profile in ("management", "compute_1rw", "compute_1r1w"):
        run(f"reset-{profile}", ["--profile", profile, "--reset"])
    clock_trace = args.output / "clock-switch" / "trace.jsonl"
    run(
        "clock-switch",
        [
            "--profile",
            "management",
            "--clock-switch",
            "--latency",
            "3",
            "--trace",
            str(clock_trace),
        ],
    )
    cases[-1]["clock_transition"] = clock_check(clock_trace)
    cases[-1]["trace_sha256"] = fingerprint(clock_trace)
    sources = list((ROOT / "gem5-ext/src/gw100/sram").rglob("*"))
    sources += list((ROOT / "gem5-ext/tests/integration/sram").glob("*"))
    sources += [
        Path(__file__).resolve(),
        ROOT / "spec-src/sram/gw100-sram.json",
        ROOT / "gem5-ext/configs/sram_fixture.py",
        args.gem5,
    ]
    report = {
        "cases": cases,
        "seed": 0,
        "tick_ps": 1,
        "sources": {
            str(p.relative_to(ROOT)): fingerprint(p)
            for p in sources
            if p.is_file()
            and (
                p.suffix in {".cc", ".hh", ".py", ".json", ".opt"}
                or p.name == "SConscript"
            )
        },
    }
    (args.output / "results.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    )
    print(f"SRAM timing 与恢复 {len(cases)}/{len(cases)} PASS")


if __name__ == "__main__":
    main()
