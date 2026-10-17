"""
@file  conftest.py
@brief L0 测试的公共配置：加载 func-core 的 C 接口

func-core 的 C 接口经 ctypes 与 numpy golden 逐位比对。
"""

import ctypes
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
LIB_PATH = REPO_ROOT / "gem5-ext" / "func-core" / "build" / "libacisfc.so"


@pytest.fixture(scope="session")
def fc():
    if not LIB_PATH.exists():
        pytest.fail(f"找不到 {LIB_PATH}，请先运行 make -C gem5-ext/func-core")
    lib = ctypes.CDLL(str(LIB_PATH))
    lib.acis_f32_to_f16.argtypes = [ctypes.c_float]
    lib.acis_f32_to_f16.restype = ctypes.c_uint16
    lib.acis_f16_to_f32.argtypes = [ctypes.c_uint16]
    lib.acis_f16_to_f32.restype = ctypes.c_float
    lib.acis_f32_to_bf16.argtypes = [ctypes.c_float]
    lib.acis_f32_to_bf16.restype = ctypes.c_uint16
    lib.acis_bf16_to_f32.argtypes = [ctypes.c_uint16]
    lib.acis_bf16_to_f32.restype = ctypes.c_float
    lib.acis_vec_binary.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                    ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t]
    lib.acis_vec_binary.restype = ctypes.c_int
    lib.acis_vec_scalar.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_float,
                                    ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t]
    lib.acis_vec_scalar.restype = ctypes.c_int
    lib.acis_decode_roundtrip.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
                                          ctypes.c_size_t]
    lib.acis_decode_roundtrip.restype = ctypes.c_int
    lib.acis_byte_runs.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint64),
                                   ctypes.POINTER(ctypes.c_uint32), ctypes.c_size_t]
    lib.acis_byte_runs.restype = ctypes.c_size_t
    lib.acis_addr_span.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint64),
                                   ctypes.POINTER(ctypes.c_uint64)]
    lib.acis_addr_span.restype = ctypes.c_int
    lib.acis_resolve_local.argtypes = [ctypes.c_char_p, ctypes.c_char_p] + [ctypes.c_uint64] * 4 + \
        [ctypes.c_char_p, ctypes.c_size_t]
    lib.acis_resolve_local.restype = ctypes.c_int
    return lib
