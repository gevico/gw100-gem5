"""
@file  test_vec_ops.py
@brief L1：向量指令端到端功能正确性

数据路径：DRAM -> (DMA) -> Tile SRAM -> 向量引擎 -> Tile SRAM -> (DMA) -> DRAM，
指令之间用本 Tile 的同步计数器衔接：
    c0：两次输入 DMA 各 +1，向量指令等 c0 >= 2
    c1：向量指令完成 +1，输出 DMA 等 c1 >= 1
SRAM 操作数默认用 Tile 相对地址（本地窗口）。

@see SPEC-220
"""

import numpy as np
import pytest
from gw100_testkit import addrmap, asm, golden
from gw100_testkit.dtypes import INT_RANGE, is_float, quantize
from gw100_testkit.harness import OpTest

pytestmark = pytest.mark.gem5

N = 4096
STATUS_ERROR = 3


def rand(rng, dtype, shape):
    if is_float(dtype):
        return quantize(rng.standard_normal(shape) * 4, dtype)
    lo, hi = INT_RANGE[dtype]
    return quantize(rng.integers(lo, hi + 1, size=shape), dtype)


def build_binary(t, tile, op, ta, tb, tc, shape, b_shape, rng):
    """在 tile 上做一次 c = a op b；返回 (输出张量, golden)。"""
    a_data, b_data = rand(rng, ta, shape), rand(rng, tb, b_shape)
    a, b, c = t.dram(shape, ta), t.dram(b_shape, tb), t.dram(shape, tc)
    t.load(a, a_data)
    t.load(b, b_data)
    sa, sb, sc = t.local(shape, ta), t.local(b_shape, tb), t.local(shape, tc)
    t.add(tile, asm.dma_copy(a, sa, signal=(0, None)))
    t.add(tile, asm.dma_copy(b, sb, signal=(0, None)))
    t.add(tile, asm.vec_binary(op, sa, sb.broadcast(shape), sc,
                               wait=(0, 2), signal=(1, None)))
    t.add(tile, asm.dma_copy(sc, c, wait=(1, 1)))
    return c, golden.vec_binary(op, a_data, ta, b_data, tb, tc)


@pytest.mark.parametrize("op,ta,tb,tc", [
    ("ADD", "FP32", "FP32", "FP32"),
    ("ADD", "FP16", "FP16", "FP16"),
    ("MUL", "BF16", "BF16", "BF16"),
    ("MAX", "FP16", "FP16", "FP16"),
    ("SUB", "INT16", "INT16", "INT16"),
    ("ADD", "INT8", "INT8", "INT8"),      # 覆盖饱和
    ("ADD", "FP16", "FP16", "FP32"),      # 混合精度
])
def test_vec_binary(op, ta, tb, tc):
    rng = np.random.default_rng(0)
    t = OpTest(f"vec_binary_{op}_{ta}_{tb}_{tc}".lower())
    c, want = build_binary(t, 0, op, ta, tb, tc, (N,), (N,), rng)
    t.add(0, asm.end())
    t.expect("c", c, want)
    r = t.run()
    r.check()
    print(f"{t.name}: {r.device_cycles:.0f} cycles")


def test_bias_broadcast():
    """b 的第 0 维 stride 为 0：每一行都加同一个偏置。"""
    rng = np.random.default_rng(1)
    t = OpTest("vec_bias_broadcast")
    c, want = build_binary(t, 0, "ADD", "FP16", "FP16", "FP16", (64, 64), (1, 64), rng)
    t.add(0, asm.end())
    t.expect("c", c, want)
    t.run().check()


def test_vec_scalar_global_address():
    """SRAM 操作数也可以用本 Tile 的全局地址；数据后门装进 SRAM、结果直接从 SRAM 读回。"""
    rng = np.random.default_rng(2)
    tile = 5
    t = OpTest("vec_scalar_global_addr")
    a_data = rand(rng, "FP16", (N,))
    sa, sc = t.sram(tile, (N,), "FP16"), t.sram(tile, (N,), "FP16")
    t.load(sa, a_data)
    t.add(tile, asm.vec_scalar("MUL", sa, 0.125, sc))
    t.add(tile, asm.end())
    t.expect("c", sc, golden.vec_scalar("MUL", a_data, "FP16", 0.125, "FP16"))
    t.run().check()


def test_spmd_all_tiles():
    """16 个 Tile 各算一段：DMA 按 Tile 分别下发（DRAM 地址不同），
    向量指令和 END 各只下发一条，多播给全部 Tile。"""
    rng = np.random.default_rng(3)
    t = OpTest("vec_add_spmd_all_tiles")
    tiles = [tid for tid, _, _ in addrmap.compute_tiles(t.p)]
    sa, sb, sc = (t.local((N,), "FP16") for _ in range(3))
    outs = []
    for tile in tiles:
        a_data, b_data = rand(rng, "FP16", (N,)), rand(rng, "FP16", (N,))
        a, b, c = (t.dram((N,), "FP16") for _ in range(3))
        t.load(a, a_data)
        t.load(b, b_data)
        t.add(tile, asm.dma_copy(a, sa, signal=(0, None)))
        t.add(tile, asm.dma_copy(b, sb, signal=(0, None)))
        outs.append((c, golden.vec_binary("ADD", a_data, "FP16", b_data, "FP16", "FP16")))
    t.add(tiles, asm.vec_binary("ADD", sa, sb, sc, wait=(0, 2), signal=(1, None)))
    for tile, (c, _) in zip(tiles, outs):
        t.add(tile, asm.dma_copy(sc, c, wait=(1, 1)))
    t.add(tiles, asm.end())
    for tile, (c, want) in zip(tiles, outs):
        t.expect(f"c{tile}", c, want)
    r = t.run()
    r.check()
    print(f"{t.name}: {r.device_cycles:.0f} cycles，"
          f"派发 {r.stats.get('system.dispatcher.dispatch.instsDispatched', 0):.0f} 条")


def test_local_overflow_rejected():
    """本地地址越出 SRAM 容量：派发器校验程序时报错，不执行任何指令。"""
    t = OpTest("vec_local_overflow")
    sram = addrmap.tile_sram_size(t.p)
    bad = asm.Tensor(addrmap.local_sram_base(t.p) + sram - 64, (N,), "FP16")
    t.add(0, asm.vec_scalar("ADD", bad, 1.0, bad))
    t.add(0, asm.end())
    r = t.run()
    assert r.status == STATUS_ERROR
    assert "越出本 Tile SRAM" in (r.pkg_dir / "gem5.log").read_text(encoding="utf-8")


def test_bad_magic_rejected():
    """程序头的 magic 不对（例如旧格式的 "ACIS"）：派发器拒绝执行，返回出错状态。"""
    t = OpTest("vec_bad_magic")
    t.add(0, asm.end())
    good = t.program.encode()
    t.program.encode = lambda: b"ACIS" + good[4:]
    r = t.run()
    assert r.status == STATUS_ERROR
    assert "magic" in (r.pkg_dir / "gem5.log").read_text(encoding="utf-8")
