"""
@file  test_inst.py
@brief L0：汇编器与 C++ 译码器的编码一致性，以及张量地址展开

@see SPEC-230
"""

import ctypes
import itertools

import numpy as np
import pytest

from acis import asm
from acis.dtypes import elem_bits


def _roundtrip(fc, raw):
    out = ctypes.create_string_buffer(len(raw))
    err = ctypes.create_string_buffer(256)
    ok = fc.acis_decode_roundtrip(raw, out, err, len(err))
    assert ok, err.value.decode()
    return out.raw


def test_encode_decode_roundtrip(fc):
    a = asm.Tensor(0x10_0000_0000, (64, 128), "FP16")
    b = asm.Tensor(0x10_0000_4000, (128,), "FP16").broadcast((64, 128))
    c = asm.Tensor(0x10_0001_0000, (64, 128), "BF16")
    insts = [
        asm.vec_binary("MAX", a, b, c, wait=(3, 7), signal=(5, 12)),
        asm.vec_scalar("MUL", a, 0.125, c, signal=(1, None)),
        asm.dma_copy(asm.Tensor(0x100, (4, 8, 16, 32), "INT8"),
                     asm.Tensor(0x10_0000_0000, (4, 8, 16, 32), "INT8")),
        asm.sync(wait=(31, 65535)),
        asm.end(),
    ]
    for inst in insts:
        raw = inst.encode()
        assert len(raw) == 128
        assert _roundtrip(fc, raw) == raw


def test_decode_rejects_bad_opcode(fc):
    raw = bytearray(asm.nop().encode())
    raw[0] = 0x7F
    err = ctypes.create_string_buffer(256)
    assert not fc.acis_decode_roundtrip(bytes(raw), ctypes.create_string_buffer(128), err, 256)


def _py_runs(t):
    """byteRuns 的独立 Python 实现，用来对照。"""
    addr, size, stride = t.encode_fields()
    bits = elem_bits(t.dtype)
    eb = bits // 8
    contig0 = bits == 4 or stride[0] == eb
    row_bytes = (size[0] * bits + 7) // 8
    runs = []

    def push(a, n):
        if runs and runs[-1][0] + runs[-1][1] == a:
            runs[-1][1] += n
        else:
            runs.append([a, n])

    for i3, i2, i1 in itertools.product(range(size[3]), range(size[2]), range(size[1])):
        row = addr + i3 * stride[3] + i2 * stride[2] + i1 * stride[1]
        if contig0:
            push(row, row_bytes)
        else:
            for i0 in range(size[0]):
                push(row + i0 * stride[0], eb)
    return [tuple(r) for r in runs]


@pytest.mark.parametrize("seed", range(20))
def test_byte_runs(fc, seed):
    rng = np.random.default_rng(seed)
    dtype = rng.choice(["INT8", "FP16", "FP32", "INT4"])
    ndim = int(rng.integers(1, 5))
    shape = tuple(int(x) for x in rng.integers(1, 6, ndim))
    if dtype == "INT4":
        shape = shape[:-1] + (shape[-1] * 2,)
        t = asm.Tensor(0x1000, shape, dtype)
    else:
        eb = elem_bits(dtype) // 8
        strides = tuple(int(rng.choice([0, eb, eb * 3, eb * 64])) for _ in shape)
        t = asm.Tensor(0x1000, shape, dtype, strides)
    raw = asm.vec_scalar("ADD", t, 0.0, t).encode() if dtype != "INT4" else \
        asm.dma_copy(t, t).encode()
    cap = 4096
    addrs = (ctypes.c_uint64 * cap)()
    lens = (ctypes.c_uint32 * cap)()
    n = fc.acis_byte_runs(raw, 0, addrs, lens, cap)
    got = [(addrs[i], lens[i]) for i in range(n)]
    assert got == _py_runs(t)


def _span(fc, raw, k):
    lo, hi = ctypes.c_uint64(), ctypes.c_uint64()
    assert fc.acis_addr_span(raw, k, ctypes.byref(lo), ctypes.byref(hi))
    return lo.value, hi.value


@pytest.mark.parametrize("seed", range(20))
def test_addr_span_matches_byte_runs(fc, seed):
    """addrSpan 必须恰好覆盖 byteRuns 列出的所有字节（含负 stride）。"""
    rng = np.random.default_rng(100 + seed)
    dtype = str(rng.choice(["INT8", "FP16", "FP32"]))
    eb = elem_bits(dtype) // 8
    ndim = int(rng.integers(1, 5))
    shape = tuple(int(x) for x in rng.integers(1, 6, ndim))
    strides = tuple(int(rng.choice([0, eb, -eb, eb * 3, -eb * 64])) for _ in shape)
    t = asm.Tensor(0x10_0000, shape, dtype, strides)
    raw = asm.vec_scalar("ADD", t, 0.0, t).encode()
    runs = _py_runs(t)
    assert _span(fc, raw, 0) == (min(a for a, _ in runs), max(a + n for a, n in runs))


# 本地窗口参数：独立于 arch_params 取值，便于看出换算
WIN, WIN_BYTES, SRAM, SRAM_BYTES = 0xF_0000_0000, 0x100_0000, 0x10_0300_0000, 0x20_0000


def _resolve(fc, inst):
    out = ctypes.create_string_buffer(128)
    err = ctypes.create_string_buffer(256)
    ok = fc.acis_resolve_local(inst.encode(), out, WIN, WIN_BYTES, SRAM, SRAM_BYTES, err, 256)
    return ok, out.raw, err.value.decode()


def _opnd_addr(raw, k):
    from acis.isa_defs import OPERAND
    off = OPERAND["base"] + k * OPERAND["bytes"]
    return int.from_bytes(raw[off:off + 8], "little")


def test_resolve_local_translates_window_only(fc):
    a = asm.Tensor(WIN + 0x100, (64,), "FP16")
    b = asm.Tensor(0x2000, (64,), "FP16")                 # DRAM，不换算
    c = asm.Tensor(0x10_0000_0000 + 0x40, (64,), "FP16")  # 别的 Tile 的全局地址，不换算
    ok, raw, err = _resolve(fc, asm.vec_binary("ADD", a, b, c))
    assert ok, err
    assert [_opnd_addr(raw, k) for k in range(3)] == [SRAM + 0x100, 0x2000, 0x10_0000_0040]


def test_resolve_local_rejects_overflow(fc):
    # 最后一个元素恰好落在 SRAM 末尾：合法
    n = 1024
    edge = asm.Tensor(WIN + SRAM_BYTES - 2 * n, (n,), "FP16")
    assert _resolve(fc, asm.vec_scalar("ADD", edge, 1.0, edge))[0]
    # 多出一个元素：越界（虽然仍在 16 MiB 窗口内）
    over = asm.Tensor(WIN + SRAM_BYTES - 2 * n + 2, (n,), "FP16")
    ok, _, err = _resolve(fc, asm.vec_scalar("ADD", over, 1.0, over))
    assert not ok and "越出" in err
    # 负 stride 走到窗口起点之前：越界
    neg = asm.Tensor(WIN + 0x10, (4,), "FP32", (-8,))
    assert not _resolve(fc, asm.vec_scalar("ADD", neg, 1.0, neg))[0]
