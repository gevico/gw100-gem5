"""
@file  bmtest.py
@brief 批量运行裸机测试并汇总结果

由顶层 `make test` 调用。test 列表取自 `make -C baremetal list`；每个
(test, host 预设) 组合在独立的 gem5 进程和输出目录中运行，互不影响。
gem5 的进程退出码就是裸机程序的退出码：0 PASS、1 FAIL、2 CRASH；
到达仿真 tick 上限（124）或墙钟超时记为 TIMEOUT，其他记为 ERROR。

用法见 baremetal/README.md。
"""

import argparse
import concurrent.futures
import fnmatch
import pathlib
import subprocess
import sys
import time

RESULTS = {0: "PASS", 1: "FAIL", 2: "CRASH", 124: "TIMEOUT"}
TAIL_LINES = 15


def list_tests(baremetal_dir: pathlib.Path) -> list[str]:
    out = subprocess.run(
        ["make", "-s", "-C", str(baremetal_dir), "list"],
        check=True, capture_output=True, text=True,
    ).stdout
    return [t for t in out.split() if t]


def select(tests: list[str], patterns: list[str]) -> list[str]:
    if not patterns:
        return tests
    chosen = [t for t in tests if any(fnmatch.fnmatch(t, p) for p in patterns)]
    unknown = [p for p in patterns
               if not any(fnmatch.fnmatch(t, p) for t in tests)]
    if unknown:
        sys.exit(f"没有匹配的 test：{' '.join(unknown)}")
    return chosen


def run_one(args, test: str, host: str) -> tuple[str, str, str, pathlib.Path, float]:
    outdir = args.outdir / host / test
    outdir.mkdir(parents=True, exist_ok=True)
    log = outdir / "test.log"
    elf = args.build_dir / f"{test}.elf"
    cmd = [
        str(args.gem5), "-q", f"--outdir={outdir}", str(args.config),
        "--board", args.board, "--host", host, "--device", args.device,
        "--workload", "baremetal", "--kernel", str(elf),
        "--max-ticks", str(args.max_ticks),
    ]
    start = time.monotonic()
    with log.open("w", encoding="utf-8") as f:
        f.write("$ " + " ".join(cmd) + "\n")
        f.flush()
        try:
            rc = subprocess.run(cmd, cwd=args.gem5_root, stdout=f,
                                stderr=subprocess.STDOUT,
                                timeout=args.timeout).returncode
            result = RESULTS.get(rc, f"ERROR({rc})")
        except subprocess.TimeoutExpired:
            f.write(f"\n墙钟超时（{args.timeout} s），已终止\n")
            result = "TIMEOUT"
    return test, host, result, log, time.monotonic() - start


def tail(path: pathlib.Path, n: int) -> str:
    """失败时展示的日志：优先挑出测试程序的输出和退出原因，滤掉 gem5 的常规告警。"""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    keep = [line for line in lines
            if line.startswith("[") or line.startswith("bare-metal exit")
            or "panic" in line or "fatal" in line or "墙钟超时" in line]
    return "\n".join("    " + line for line in (keep or lines)[-n:])


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--gem5", type=pathlib.Path, required=True, help="gem5.opt 路径")
    p.add_argument("--config", type=pathlib.Path, required=True, help="gem5 配置脚本")
    p.add_argument("--baremetal-dir", type=pathlib.Path, default=pathlib.Path("baremetal"))
    p.add_argument("--build-dir", type=pathlib.Path, required=True, help="test ELF 所在目录")
    p.add_argument("--outdir", type=pathlib.Path, required=True, help="各次运行的输出根目录")
    p.add_argument("--board", default="arm_vexpress")
    p.add_argument("--device", default="gw100")
    p.add_argument("--hosts", default="timing atomic", help="空格分隔的 host 预设")
    p.add_argument("--tests", default="", help="空格分隔的 test 名，支持通配；空表示全部")
    p.add_argument("-j", "--jobs", type=int, default=4)
    p.add_argument("--timeout", type=int, default=300, help="每次运行的墙钟超时，秒")
    p.add_argument("--max-ticks", type=int, default=10_000_000_000,
                   help="每次运行的仿真 tick 上限")
    p.add_argument("--list", action="store_true", help="只列出将要运行的 test")
    args = p.parse_args()

    args.gem5 = args.gem5.resolve()
    args.gem5_root = args.gem5.parents[2]       # <gem5>/build/ARM/gem5.opt
    args.config = args.config.resolve()
    args.build_dir = args.build_dir.resolve()
    args.outdir = args.outdir.resolve()

    tests = select(list_tests(args.baremetal_dir), args.tests.split())
    hosts = args.hosts.split()
    if args.list:
        print("\n".join(tests))
        return 0

    missing = [t for t in tests if not (args.build_dir / f"{t}.elf").is_file()]
    if missing:
        sys.exit(f"找不到 ELF，请先编译：{' '.join(missing)}")

    jobs = [(t, h) for t in tests for h in hosts]
    print(f"运行 {len(tests)} 个 test × {len(hosts)} 个 host 预设，并行 {args.jobs}")
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, args, t, h) for t, h in jobs]
        for fut in concurrent.futures.as_completed(futures):
            test, host, result, log, secs = fut.result()
            results[(test, host)] = (result, log)
            print(f"  {result:<9} {test} [{host}]  {secs:.1f} s")

    width = max(len(t) for t in tests)
    print("\n汇总：")
    print("  " + "test".ljust(width) + "".join(f"  {h:<9}" for h in hosts))
    for t in tests:
        print("  " + t.ljust(width) +
              "".join(f"  {results[(t, h)][0]:<9}" for h in hosts))

    failed = [(k, v) for k, v in results.items() if v[0] != "PASS"]
    for (test, host), (result, log) in sorted(failed):
        print(f"\n{result}: {test} [{host}]，日志 {log}")
        print(tail(log, TAIL_LINES))

    passed = len(results) - len(failed)
    print(f"\n{passed}/{len(results)} 通过")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
