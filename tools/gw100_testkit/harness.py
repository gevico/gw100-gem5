"""
@file  harness.py
@brief 算子测试框架：生成测试包、调用 gem5、读回结果并与 golden 比对

典型用法：

    t = OpTest("vec_add_fp16")
    a = t.dram((4096,), "FP16"); t.load(a, a_data)
    sa = t.local((4096,), "FP16")             # Tile 相对地址：每个 Tile 同一偏移
    ...
    t.add(0, asm.dma_copy(a, sa, signal=(0, None)))
    t.add([0, 1, 2, 3], asm.vec_binary(...))  # 一条指令多播给 4 个 Tile
    ...
    t.expect("c", c, golden.vec_binary("ADD", a_data, "FP16", b_data, "FP16", "FP16"))
    r = t.run()
    r.check()

SRAM 张量有两种分配方式：
- local(shape, dtype)：本地窗口地址，用在多播指令里；后门加载/读回时要用 tile= 指明哪个 Tile；
- sram(tile, shape, dtype)：该 Tile SRAM 的全局地址，用于跨 Tile 访问。
两者共用同一个偏移分配器，所以在任何 Tile 上都不会重叠。

gem5 的运行方式，按以下顺序选择：
1. 环境变量 GEM5_BIN 指向的可执行文件，直接运行；
2. ACIS_GEM5_DOCKER=1 时强制走 Docker（与容器环境对照时用）；
3. 本平台原生构建的 gem5，直接运行：Linux 为 gem5/build/NULL/gem5.opt，
   macOS 为 gem5/build/NULL_DARWIN/gem5.opt（构建命令见 CLAUDE.md）；
4. 都没有时，在 Docker 镜像 ACIS_GEM5_IMAGE（默认取本机已有的 gem5-slim 或
   gem5-dev）里运行 gem5/build/NULL/gem5.opt，仓库根目录按原路径挂载进容器。
直接运行时每个用例只启动一个 gem5 进程，不再每次启动容器。

@see MAN-060
"""

import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path

import numpy as np

from . import addrmap
from .asm import Program, Tensor, command_record, parse_completion
from .dtypes import dense_bytes, from_bytes, is_float, to_bytes, to_float

REPO_ROOT = addrmap.REPO_ROOT
GEM5_DIR = REPO_ROOT / "gem5"
RUN_OP = REPO_ROOT / "gem5-ext" / "configs" / "run_op.py"
# 容器里构建的是 Linux 版；各平台原生构建的 gem5 放在不同的变体目录，互不覆盖
LINUX_GEM5_BIN = GEM5_DIR / "build" / "NULL" / "gem5.opt"
NATIVE_GEM5_BIN = {"linux": LINUX_GEM5_BIN,
                   "darwin": GEM5_DIR / "build" / "NULL_DARWIN" / "gem5.opt"}
DEFAULT_OUT_DIR = REPO_ROOT / "build" / "op-tests"

# Host 内存窗口内的固定布局
CMD_OFFSET = 0x0
CPL_OFFSET = 0x100
PROG_OFFSET = 0x1000

STATUS_DONE = 2
ALIGN = 64


class Gem5Error(RuntimeError):
    pass


# ---------------------------------------------------------------- gem5 调用

_IMAGE_CANDIDATES = ("gem5-slim", "gem5-dev")


@lru_cache(maxsize=None)
def _local_images():
    """本机已有的镜像名。用 image ls 而不用 image inspect：
    在 Docker 29 的 containerd 镜像存储下，inspect 偶尔找不到 ls 能列出的镜像。"""
    r = subprocess.run(["docker", "image", "ls", "--format", "{{.Repository}}"],
                       capture_output=True, text=True)
    return set(r.stdout.split()) if r.returncode == 0 else set()


@lru_cache(maxsize=None)
def _docker_image():
    """ACIS_GEM5_IMAGE 优先；否则用本机已有的第一个候选镜像（两者工具链相同）。"""
    if os.environ.get("ACIS_GEM5_IMAGE"):
        return os.environ["ACIS_GEM5_IMAGE"]
    for image in _IMAGE_CANDIDATES:
        if image in _local_images():
            return image
    return _IMAGE_CANDIDATES[0]


def gem5_runner():
    """返回 ("native", 可执行文件) 或 ("docker", None)，选择顺序见模块说明。"""
    if os.environ.get("GEM5_BIN"):
        return ("native", Path(os.environ["GEM5_BIN"]))
    if os.environ.get("ACIS_GEM5_DOCKER") != "1":
        native = NATIVE_GEM5_BIN.get(sys.platform)
        if native and native.exists():
            return ("native", native)
    return ("docker", None)


def gem5_available():
    """返回 (是否可用, 原因)。供测试在没有 gem5 时跳过。"""
    kind, binary = gem5_runner()
    if kind == "native":
        return (binary.exists(), f"{binary} 不存在")
    if not LINUX_GEM5_BIN.exists():
        return (False, f"没有本平台原生构建的 gem5，也没有容器里构建的 {LINUX_GEM5_BIN}")
    if shutil.which("docker") is None:
        return (False, "找不到 docker")
    if _docker_image().split(":")[0] not in _local_images():
        return (False, f"本机没有 Docker 镜像 {_docker_image()}")
    return (True, "")


def gem5_command(pkg_dir, debug_flags=None):
    pkg_dir = Path(pkg_dir).resolve()
    args = [f"--outdir={pkg_dir / 'm5out'}"]
    if debug_flags:
        args.append(f"--debug-flags={','.join(debug_flags)}")
    args += [str(RUN_OP), str(pkg_dir)]
    kind, binary = gem5_runner()
    if kind == "native":
        return [str(binary)] + args
    root = str(REPO_ROOT)
    return ["docker", "run", "--rm", "-v", f"{root}:{root}", "-w", root,
            _docker_image(), str(LINUX_GEM5_BIN)] + args


def parse_stats(path):
    """读取 stats.txt 的第一段统计，返回 {名字: 数值}。"""
    stats = {}
    in_dump = False
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if line.startswith("---------- Begin"):
            in_dump = True
            continue
        if line.startswith("---------- End"):
            break
        if not in_dump:
            continue
        parts = line.split("#", 1)[0].split()
        if len(parts) < 2:
            continue
        try:
            stats[parts[0]] = float(parts[1])
        except ValueError:
            pass
    return stats


# ---------------------------------------------------------------- 测试描述

class _Bump:
    def __init__(self, base, size):
        self.base, self.size, self.next = base, size, base

    def alloc(self, nbytes):
        addr = self.next
        self.next = (addr + nbytes + ALIGN - 1) // ALIGN * ALIGN
        if self.next > self.base + self.size:
            raise MemoryError(f"区域 {self.base:#x} 空间不足（需要 {nbytes} 字节）")
        return addr


@dataclass
class _Check:
    name: str
    tensor: Tensor
    expected: np.ndarray
    rtol: float
    atol: float


def _require_dense(t):
    if tuple(t.strides) != t.contiguous_strides():
        raise ValueError(f"后门加载/读回只支持紧密排列的张量：{t}")


class OpTest:
    """一个算子测试：输入数据、程序、期望输出。"""

    def __init__(self, name, params=None, functional=True, timeout="100ms"):
        self.name = name
        self.p = params or addrmap.params()
        self.functional = functional
        self.timeout = timeout
        self.program = Program()
        self._loads = []
        self._checks = []
        self._dram = _Bump(addrmap.dram_base(self.p), self.p["address_map"]["dram_size"])
        self._sram = _Bump(0, addrmap.tile_sram_size(self.p))   # 分配的是 SRAM 内偏移

    # ---- 分配
    def dram(self, shape, dtype):
        n = int(np.prod(shape))
        return Tensor(self._dram.alloc(dense_bytes(dtype, n)), shape, dtype)

    def local(self, shape, dtype):
        n = int(np.prod(shape))
        off = self._sram.alloc(dense_bytes(dtype, n))
        return Tensor(addrmap.local_sram_base(self.p) + off, shape, dtype)

    def sram(self, tile, shape, dtype):
        n = int(np.prod(shape))
        off = self._sram.alloc(dense_bytes(dtype, n))
        return Tensor(addrmap.tile_sram_base(tile, self.p) + off, shape, dtype)

    def _global(self, t, tile):
        """本地窗口张量 -> 指定 Tile 上的全局地址张量。"""
        if not addrmap.is_local(t.addr, self.p):
            return t
        if tile is None:
            raise ValueError(f"{t} 是 Tile 相对地址，后门访问时必须用 tile= 指明 Tile")
        return Tensor(addrmap.to_global(t.addr, tile, self.p), t.shape, t.dtype, t.strides)

    # ---- 数据与程序
    def load(self, t, stored, tile=None):
        """tile 可以是一个编号或编号列表（同一份数据装进多个 Tile）。"""
        _require_dense(t)
        raw = to_bytes(np.asarray(stored).reshape(t.shape), t.dtype)
        for k in (tile if isinstance(tile, (list, tuple, range)) else [tile]):
            self._loads.append((self._global(t, k).addr, raw))

    def add(self, tiles, inst):
        self.program.add(tiles, inst)
        return self

    def expect(self, name, t, expected, rtol=0.0, atol=0.0, tile=None):
        """rtol = atol = 0 时要求逐位一致；浮点 NaN 只要求位置一致，不比较载荷。"""
        _require_dense(t)
        t = self._global(t, tile)
        self._checks.append(_Check(name, t, np.asarray(expected).reshape(t.shape), rtol, atol))

    # ---- 测试包
    def write_package(self, pkg_dir):
        pkg_dir = Path(pkg_dir)
        if pkg_dir.exists():
            shutil.rmtree(pkg_dir)
        (pkg_dir / "data").mkdir(parents=True)

        host = addrmap.host_mem_base(self.p)
        prog = self.program.encode()
        loads = list(self._loads)
        loads.append((host + PROG_OFFSET, prog))
        loads.append((host + CMD_OFFSET, command_record(
            host + PROG_OFFSET, len(prog), host + CPL_OFFSET, tag=1)))

        pkg = {
            "name": self.name,
            "arch_params": self.p,
            "functional": self.functional,
            "timeout": self.timeout,
            "cmd_record_addr": host + CMD_OFFSET,
            "completion_addr": host + CPL_OFFSET,
            "loads": [],
            "dumps": [],
        }
        for i, (addr, raw) in enumerate(loads):
            f = f"data/in{i}.bin"
            (pkg_dir / f).write_bytes(raw)
            pkg["loads"].append({"addr": addr, "file": f})
        for c in self._checks:
            f = f"data/out_{c.name}.bin"
            pkg["dumps"].append({"addr": c.tensor.addr, "file": f,
                                 "bytes": dense_bytes(c.tensor.dtype, c.tensor.numel)})
        pkg["dumps"].append({"addr": host + CPL_OFFSET, "bytes": 32,
                             "file": "data/completion.bin"})
        (pkg_dir / "package.json").write_text(json.dumps(pkg, indent=2, default=str),
                                              encoding="utf-8")
        return pkg_dir

    def run(self, out_dir=None, debug_flags=None, timeout_s=1800):
        pkg_dir = Path(out_dir) if out_dir else DEFAULT_OUT_DIR / self.name
        self.write_package(pkg_dir)
        cmd = gem5_command(pkg_dir, debug_flags)
        log = pkg_dir / "gem5.log"
        with open(log, "w", encoding="utf-8") as f:
            f.write(" ".join(cmd) + "\n")
            f.flush()
            proc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT,
                                  timeout=timeout_s)
        result_file = pkg_dir / "result.json"
        if proc.returncode not in (0, 1) or not result_file.exists():
            tail = "\n".join(log.read_text(encoding="utf-8", errors="replace")
                             .splitlines()[-40:])
            raise Gem5Error(f"gem5 退出码 {proc.returncode}，日志 {log}：\n{tail}")
        return RunResult.collect(self, pkg_dir)


# ---------------------------------------------------------------- 结果

@dataclass
class Mismatch:
    name: str
    count: int
    total: int
    first: list = field(default_factory=list)   # [(扁平下标, 实际值, 期望值)]

    def __str__(self):
        head = ", ".join(f"[{i}] {a} != {e}" for i, a, e in self.first)
        return f"{self.name}: {self.count}/{self.total} 个元素不一致；{head}"


def _compare(c, raw):
    t = c.tensor
    actual = from_bytes(raw, t.dtype, t.shape)
    if c.rtol == 0 and c.atol == 0:
        if t.dtype == "INT4":
            bad_elems = actual.reshape(-1) != c.expected.reshape(-1)
        else:
            esz = dense_bytes(t.dtype, 1)
            ab = np.frombuffer(to_bytes(actual, t.dtype), np.uint8).reshape(-1, esz)
            eb = np.frombuffer(to_bytes(c.expected, t.dtype), np.uint8).reshape(-1, esz)
            bad_elems = (ab != eb).any(axis=1)
        if is_float(t.dtype):
            # 与 L0 的约定一致：NaN 只要求两边都是 NaN，不比较载荷
            an = np.isnan(to_float(actual, t.dtype).reshape(-1))
            en = np.isnan(to_float(c.expected, t.dtype).reshape(-1))
            bad_elems = np.where(an & en, False, bad_elems | (an != en))
    else:
        x = to_float(actual, t.dtype).reshape(-1)
        y = to_float(c.expected, t.dtype).reshape(-1)
        bad_elems = ~np.isclose(x, y, rtol=c.rtol, atol=c.atol, equal_nan=True)
    idx = np.flatnonzero(bad_elems)
    if idx.size == 0:
        return None
    a = to_float(actual, t.dtype).reshape(-1) if is_float(t.dtype) else actual.reshape(-1)
    e = to_float(c.expected, t.dtype).reshape(-1) if is_float(t.dtype) \
        else c.expected.reshape(-1)
    return Mismatch(c.name, int(idx.size), t.numel, [(int(i), a[i], e[i]) for i in idx[:8]])


@dataclass
class RunResult:
    name: str
    pkg_dir: Path
    status: int
    device_ticks: int
    tick_freq: int
    clock_hz: float
    outputs: dict
    mismatches: list
    stats: dict

    @property
    def device_cycles(self):
        return self.device_ticks * self.clock_hz / self.tick_freq

    @property
    def ok(self):
        return self.status == STATUS_DONE and not self.mismatches

    def check(self):
        if self.status != STATUS_DONE:
            raise AssertionError(f"{self.name}: 设备状态 {self.status}（期望 {STATUS_DONE}），"
                                 f"见 {self.pkg_dir / 'gem5.log'}")
        if self.mismatches:
            raise AssertionError("\n".join(str(m) for m in self.mismatches))

    @classmethod
    def collect(cls, test, pkg_dir):
        res = json.loads((pkg_dir / "result.json").read_text(encoding="utf-8"))
        cpl = parse_completion((pkg_dir / "data" / "completion.bin").read_bytes())
        if cpl["status"] != res["status"]:
            raise Gem5Error(f"完成记录状态 {cpl['status']} 与 TestHost 报告 {res['status']} 不一致")
        outputs, mismatches = {}, []
        for c in test._checks:
            raw = (pkg_dir / "data" / f"out_{c.name}.bin").read_bytes()
            outputs[c.name] = from_bytes(raw, c.tensor.dtype, c.tensor.shape)
            m = _compare(c, raw)
            if m:
                mismatches.append(m)
        stats_file = pkg_dir / "m5out" / "stats.txt"
        stats = parse_stats(stats_file) if stats_file.exists() else {}
        return cls(test.name, pkg_dir, res["status"], res["device_ticks"], res["tick_freq"],
                   test.p["clock"]["freq_ghz"] * 1e9, outputs, mismatches, stats)
