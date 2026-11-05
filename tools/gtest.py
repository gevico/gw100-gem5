"""
@file  gtest.py
@brief 枚举、构建并运行 gem5-ext 的全部 GTest 单元测试

从 gem5-ext/tests/unit/ 下各 SConscript 的 GTest('<名字>', ...) 声明枚举
测试程序，用一次 scons 构建全部程序，再逐个运行并汇总：发现的程序数（并与
.test.cc 文件数对照）、构建出的程序数、运行的程序数，以及用例的通过数 /
总数。任一程序构建或运行失败、用例失败，或者各项数目不一致时，退出码为 1。

在仓库根目录运行（先 make gem-build，构建前会确认 gem5 补丁已应用）：
    .venv/bin/python tools/gtest.py [--jobs N] [--no-build]

scons 与 PYTHON_CONFIG 的取法与顶层 Makefile 相同。用法见 gem5-ext/README.md
的"测试"。
"""

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

REPO = pathlib.Path(__file__).resolve().parent.parent
UNIT_DIR = REPO / "gem5-ext" / "tests" / "unit"
GEM5_DIR = REPO / "gem5"
GEM5_BUILD = "build/GW100"
GTEST_DECL = re.compile(r"""GTest\(\s*['"]([^'"]+)['"]""")
RAN = re.compile(r"\[==========\] (\d+) tests? from \d+ test suites? ran")
PASSED = re.compile(r"\[  PASSED  \] (\d+) tests?")


def discover() -> list[tuple[str, str]]:
    """返回（程序名，scons 目标）列表，按 SConscript 路径排序。"""
    progs = []
    for sc in sorted(UNIT_DIR.rglob("SConscript")):
        rel = sc.parent.relative_to(REPO).as_posix()
        for m in GTEST_DECL.finditer(sc.read_text(encoding="utf-8")):
            progs.append((f"{rel}/{m.group(1)}",
                          f"{GEM5_BUILD}/{rel}/{m.group(1)}.opt"))
    return progs


def python_config() -> str | None:
    """与 Makefile 相同：环境变量优先，否则用当前 Python 版本的 -config。"""
    if os.environ.get("PYTHON_CONFIG"):
        return os.environ["PYTHON_CONFIG"]
    ver = "%d.%d" % sys.version_info[:2]
    return shutil.which(f"python{ver}-config")


def build(targets: list[str], jobs: int) -> bool:
    """应用 gem5 补丁后用一次 scons 构建全部目标；成功返回 True。"""
    subprocess.run(["make", "--no-print-directory", "gem5-patch"],
                   cwd=REPO, check=True)
    env = dict(os.environ)
    pc = python_config()
    if pc:
        env["PYTHON_CONFIG"] = pc
    scons = str(REPO / ".venv" / "bin" / "scons")
    cmd = [scons, "--ignore-style", f"EXTRAS={REPO / 'gem5-ext'}",
           f"-j{jobs}", *targets]
    return subprocess.run(cmd, cwd=GEM5_DIR, env=env).returncode == 0


def run(target: str) -> tuple[bool, int, int, float, str]:
    """运行一个程序；返回（通过、用例通过数、用例数、耗时、输出）。"""
    t0 = time.monotonic()
    p = subprocess.run([str(GEM5_DIR / target)], cwd=GEM5_DIR,
                       capture_output=True, text=True)
    dt = time.monotonic() - t0
    out = p.stdout + p.stderr
    ran = RAN.search(out)
    passed = PASSED.search(out)
    total = int(ran.group(1)) if ran else 0
    npass = int(passed.group(1)) if passed else 0
    ok = p.returncode == 0 and ran is not None and npass == total
    return ok, npass, total, dt, out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--no-build", action="store_true",
                    help="不调用 scons，只运行已构建的程序")
    args = ap.parse_args()

    progs = discover()
    sources = sorted(UNIT_DIR.rglob("*.test.cc"))
    print(f"[GTEST] 发现 {len(progs)} 个程序"
          f"（{len(sources)} 个 .test.cc）")
    if not progs:
        return 1
    if not args.no_build and not build([t for _, t in progs], args.jobs):
        print("[GTEST] 构建失败")
        return 1

    built = [(n, t) for n, t in progs if (GEM5_DIR / t).is_file()]
    for n, t in progs:
        if (n, t) not in built:
            print(f"[MISS] {n}：{t} 不存在")
    ok_progs = cases_pass = cases_total = 0
    for n, t in built:
        ok, npass, total, dt, out = run(t)
        ok_progs += ok
        cases_pass += npass
        cases_total += total
        print(f"[{'PASS' if ok else 'FAIL'}] {n}  {npass}/{total}"
              f"  {dt:.1f}s")
        if not ok:
            print("\n".join("    " + ln for ln in out.splitlines()[-20:]))

    all_ok = (len(progs) == len(sources) == len(built) == ok_progs and
              cases_pass == cases_total > 0)
    print(f"[GTEST] 程序：发现 {len(progs)}，构建 {len(built)}，"
          f"运行 {len(built)}，通过 {ok_progs}；"
          f"用例 {cases_pass}/{cases_total}；"
          f"{'PASS' if all_ok else 'FAIL'}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
