"""L0：向量运算的 C++ 实现与 numpy golden 逐位对比。"""

import ctypes

import numpy as np
import pytest

from acis import golden
from acis.dtypes import from_bytes, quantize, to_bytes
from acis.isa_defs import DTYPES, SUBOPS

N = 4096
TYPE_COMBOS = [
    ("FP32", "FP32", "FP32"),
    ("FP16", "FP16", "FP16"),
    ("BF16", "BF16", "BF16"),
    ("FP16", "FP32", "BF16"),
    ("INT32", "INT32", "INT32"),
    ("INT8", "INT8", "INT8"),
    ("INT8", "INT32", "INT16"),
]


def _data(dtype, seed):
    rng = np.random.default_rng(seed)
    if dtype in ("FP32", "FP16", "BF16"):
        v = rng.normal(0, 1000, N)
        v[:8] = [0.0, -0.0, np.inf, -np.inf, np.nan, 65504.0, 1e-6, -1e-7]
    elif dtype == "INT32":
        v = rng.integers(-(2 ** 31), 2 ** 31 - 1, N, dtype=np.int64).astype(np.float64)
    else:
        v = rng.integers(-128, 128, N).astype(np.float64)
    return quantize(v, dtype)


def _run_binary(fc, op, a, ta, b, tb, tc):
    out = ctypes.create_string_buffer(len(to_bytes(quantize(np.zeros(N), tc), tc)))
    ok = fc.acis_vec_binary(SUBOPS["VEC_BINARY"][op], DTYPES[ta]["code"], to_bytes(a, ta),
                            DTYPES[tb]["code"], to_bytes(b, tb), DTYPES[tc]["code"], out, N)
    assert ok
    return from_bytes(out.raw, tc, (N,))


def _same(got, want):
    """逐位相同；浮点 NaN 只要求都是 NaN。"""
    g = np.asarray(got)
    w = np.asarray(want)
    if g.dtype.kind == "f":
        nan = np.isnan(w)
        return np.array_equal(np.isnan(g), nan) and np.array_equal(
            g[~nan].view(np.uint8), w[~nan].view(np.uint8))
    return np.array_equal(g, w)


@pytest.mark.parametrize("op", list(SUBOPS["VEC_BINARY"]))
@pytest.mark.parametrize("ta,tb,tc", TYPE_COMBOS)
def test_vec_binary(fc, op, ta, tb, tc):
    a = _data(ta, 10)
    b = _data(tb, 11)
    got = _run_binary(fc, op, a, ta, b, tb, tc)
    want = golden.vec_binary(op, a, ta, b, tb, tc)
    if tc == "BF16":  # bf16 以 uint16 存放，NaN 的低位可能不同，只比非 NaN
        from acis.dtypes import bf16_bits_to_f32
        assert _same(bf16_bits_to_f32(got), bf16_bits_to_f32(want))
    else:
        assert _same(got, want)


@pytest.mark.parametrize("op", list(SUBOPS["VEC_SCALAR"]))
@pytest.mark.parametrize("ta,tc", [("FP32", "FP32"), ("FP16", "FP16"), ("BF16", "FP32"),
                                   ("INT32", "INT32"), ("INT8", "INT8")])
def test_vec_scalar(fc, op, ta, tc):
    a = _data(ta, 20)
    imm = 3.140625 if ta in ("FP32", "FP16", "BF16") else 7.0
    out = ctypes.create_string_buffer(len(to_bytes(quantize(np.zeros(N), tc), tc)))
    assert fc.acis_vec_scalar(SUBOPS["VEC_SCALAR"][op], DTYPES[ta]["code"], to_bytes(a, ta),
                              imm, DTYPES[tc]["code"], out, N)
    got = from_bytes(out.raw, tc, (N,))
    want = golden.vec_scalar(op, a, ta, imm, tc)
    assert _same(got, want)
