"""v0 指令的 golden 参考实现（numpy），语义与 func-core 一致，但独立实现。

规则（SPEC-240 草案）：
- 只要输入或输出有浮点类型，就先把输入舍入到 FP32，按 FP32 运算，再舍入到目的类型；
- 否则按整数运算，结果饱和到目的类型；
- max / min 遇到 NaN 时结果为 NaN。
"""

import numpy as np

from .dtypes import is_float, quantize, to_float

_BINARY = {
    "ADD": np.add,
    "SUB": np.subtract,
    "MUL": np.multiply,
    "MAX": np.maximum,   # numpy.maximum 会传播 NaN
    "MIN": np.minimum,
}

_SCALAR = {"ADD": np.add, "MUL": np.multiply, "MAX": np.maximum}


def vec_binary(op, a, ta, b, tb, tc):
    """a、b 为存储数组（形状可广播），返回目的类型的存储数组。"""
    fn = _BINARY[op]
    if is_float(ta) or is_float(tb) or is_float(tc):
        x = to_float(a, ta).astype(np.float32)
        y = to_float(b, tb).astype(np.float32)
        with np.errstate(all="ignore"):
            r = fn(x, y).astype(np.float32)
        return quantize(r, tc)
    x = to_float(a, ta)
    y = to_float(b, tb)
    return quantize(fn(x, y), tc)


def vec_scalar(op, a, ta, imm, tc):
    fn = _SCALAR[op]
    if is_float(ta) or is_float(tc):
        x = to_float(a, ta).astype(np.float32)
        with np.errstate(all="ignore"):
            # 立即数是 FP32；运算结果先舍入到 FP32
            r = fn(x.astype(np.float64), np.float64(np.float32(imm))).astype(np.float32)
        return quantize(r, tc)
    return quantize(fn(to_float(a, ta), float(np.float32(imm))), tc)


def dma_copy(a):
    return np.array(a, copy=True)
