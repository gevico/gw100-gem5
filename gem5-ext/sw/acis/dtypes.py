"""张量数据与设备字节格式之间的转换，规则与 func-core/numerics 一致（SPEC-240 草案）。"""

import numpy as np

from .isa_defs import DTYPES

NUMPY_STORAGE = {
    "INT8": np.int8,
    "UINT8": np.uint8,
    "INT16": np.int16,
    "INT32": np.int32,
    "FP16": np.float16,
    "BF16": np.uint16,   # bf16 以原始 16 位存放
    "FP32": np.float32,
}

INT_RANGE = {
    "INT8": (-128, 127),
    "UINT8": (0, 255),
    "INT16": (-32768, 32767),
    "INT32": (-(2 ** 31), 2 ** 31 - 1),
    "INT4": (-8, 7),
}


def is_float(dtype):
    return dtype in ("FP16", "BF16", "FP32")


def elem_bits(dtype):
    return DTYPES[dtype]["bits"]


def f32_to_bf16_bits(x):
    """FP32 -> BF16，就近舍入、平局取偶；NaN 保持为 quiet NaN。"""
    x = np.asarray(x, dtype=np.float32)
    u = x.view(np.uint32).astype(np.uint64)
    rounded = ((u + 0x7FFF + ((u >> 16) & 1)) >> 16).astype(np.uint16)
    nan = ((u >> 16) | 0x40).astype(np.uint16)
    return np.where(np.isnan(x), nan, rounded)


def bf16_bits_to_f32(b):
    b = np.asarray(b, dtype=np.uint16)
    return (b.astype(np.uint32) << 16).view(np.float32)


def quantize(values, dtype):
    """把 float64/float32 数组按目的类型的规则转换（舍入、饱和），返回该类型的存储数组。"""
    v = np.asarray(values)
    if dtype == "FP32":
        return v.astype(np.float32)
    if dtype == "FP16":
        return v.astype(np.float32).astype(np.float16)
    if dtype == "BF16":
        return f32_to_bf16_bits(v.astype(np.float32))
    lo, hi = INT_RANGE[dtype]
    r = np.nan_to_num(np.rint(v.astype(np.float64)), nan=0.0)
    r = np.clip(r, lo, hi).astype(np.int64)
    if dtype == "INT4":
        return r.astype(np.int8)
    return r.astype(NUMPY_STORAGE[dtype])


def to_float(stored, dtype):
    """把存储数组转成 float64，便于 golden 计算。"""
    if dtype == "BF16":
        return bf16_bits_to_f32(stored).astype(np.float64)
    return np.asarray(stored).astype(np.float64)


def to_bytes(stored, dtype):
    """存储数组 -> 紧密排列的设备字节（按行优先展平）。"""
    flat = np.asarray(stored).reshape(-1)
    if dtype == "INT4":
        nib = (flat.astype(np.int16) & 0xF).astype(np.uint8)
        if nib.size % 2:
            nib = np.append(nib, np.uint8(0))
        return (nib[0::2] | (nib[1::2] << 4)).tobytes()
    return flat.astype(NUMPY_STORAGE[dtype]).tobytes()


def from_bytes(raw, dtype, shape):
    """设备字节 -> 存储数组。"""
    n = int(np.prod(shape))
    if dtype == "INT4":
        b = np.frombuffer(raw, dtype=np.uint8)
        nib = np.empty(b.size * 2, dtype=np.int16)
        nib[0::2] = b & 0xF
        nib[1::2] = b >> 4
        nib = np.where(nib >= 8, nib - 16, nib)[:n]
        return nib.astype(np.int8).reshape(shape)
    return np.frombuffer(raw, dtype=NUMPY_STORAGE[dtype], count=n).reshape(shape).copy()


def dense_bytes(dtype, n):
    return (n * elem_bits(dtype) + 7) // 8
