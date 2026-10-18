"""
@file  test_numerics.py
@brief L0：数值转换与 numpy 参考逐位比对

@see SPEC-240
"""

import numpy as np

from gw100_testkit.dtypes import bf16_bits_to_f32, f32_to_bf16_bits

SPECIALS = np.array([0.0, -0.0, 1.0, -1.0, np.inf, -np.inf, np.nan, 65504.0, 65520.0, 65519.99,
                     6.1035156e-05, 5.9604645e-08, 2.9802322e-08, 2.9802326e-08, 1e-8, 3.4e38,
                     1.0009765625, 1.00048828125, 1.0014648, -2.0004883], dtype=np.float32)


def _random_f32(n, seed):
    rng = np.random.default_rng(seed)
    # 覆盖很宽的指数范围，包括 FP16 的非规格化区和上溢区
    mant = rng.uniform(1.0, 2.0, n)
    exp = rng.integers(-30, 20, n)
    sign = rng.choice([-1.0, 1.0], n)
    return (sign * mant * np.exp2(exp)).astype(np.float32)


def test_f32_to_f16_matches_numpy(fc):
    xs = np.concatenate([SPECIALS, _random_f32(20000, 1)])
    want = xs.astype(np.float16).view(np.uint16)
    got = np.array([fc.gw100_f32_to_f16(float(x)) for x in xs], dtype=np.uint16)
    nan = np.isnan(xs)
    assert np.array_equal(got[~nan], want[~nan])
    assert np.all(np.isnan(got[nan].view(np.float16)))


def test_f16_to_f32_all_values(fc):
    halves = np.arange(65536, dtype=np.uint32).astype(np.uint16)
    want = halves.view(np.float16).astype(np.float32)
    got = np.array([fc.gw100_f16_to_f32(int(h)) for h in halves], dtype=np.float32)
    nan = np.isnan(want)
    assert np.array_equal(got[~nan].view(np.uint32), want[~nan].view(np.uint32))
    assert np.all(np.isnan(got[nan]))


def test_bf16_roundtrip(fc):
    xs = np.concatenate([SPECIALS, _random_f32(20000, 2)])
    want = f32_to_bf16_bits(xs)
    got = np.array([fc.gw100_f32_to_bf16(float(x)) for x in xs], dtype=np.uint16)
    assert np.array_equal(got, want)
    back = np.array([fc.gw100_bf16_to_f32(int(b)) for b in want], dtype=np.float32)
    ref = bf16_bits_to_f32(want)
    nan = np.isnan(ref)
    assert np.array_equal(back[~nan].view(np.uint32), ref[~nan].view(np.uint32))
