"""
@file  addrmap.py
@brief 从架构参数 YAML 读出地址映射和 Tile 编号

原 spec-src/arch_params.yaml 已删除；重建前调用 params() 会给出明确
错误，也可以显式传入 path。

Compute Tile 按行优先编号：第 0 行从左到右是 0..3，第 1 行是 4..7，依此类推。

SRAM 有两种地址：
- 本地窗口（local_sram_base 起）：张量指令里使用，指"执行这条指令的 Tile"的 SRAM；
- 全局窗口（tile_sram_base + id × tile_sram_stride 起）：后门加载/读回、跨 Tile 访问时使用。

@see SPEC-110
"""

from functools import lru_cache
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[2]


@lru_cache(maxsize=None)
def params(path=None):
    if path is None:
        raise FileNotFoundError(
            "架构参数单一真源待重建；请显式传入 path"
        )
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(
            f"架构参数文件 {p} 不存在；单一真源待重建"
        )
    return yaml.safe_load(p.read_text(encoding="utf-8"))


def compute_tiles(p=None):
    """返回 [(tile_id, x, y)]，按行优先编号。"""
    p = p or params()
    out = []
    for y, row in enumerate(p["mesh"]["layout"]):
        for x, kind in enumerate(row):
            if kind == "CT":
                out.append((len(out), x, y))
    return out


def tile_sram_base(tile_id, p=None):
    p = p or params()
    am = p["address_map"]
    return am["tile_sram_base"] + tile_id * am["tile_sram_stride"]


def tile_sram_size(p=None):
    p = p or params()
    return p["compute_tile"]["sram"]["size_kib"] * 1024


def dram_base(p=None):
    return (p or params())["address_map"]["dram_base"]


def host_mem_base(p=None):
    return (p or params())["address_map"]["host_mem_base"]


def mgmt_mmio_base(p=None):
    return (p or params())["address_map"]["mgmt_mmio_base"]


def local_sram_base(p=None):
    return (p or params())["address_map"]["local_sram_base"]


def is_local(addr, p=None):
    am = (p or params())["address_map"]
    return am["local_sram_base"] <= addr < am["local_sram_base"] + am["local_sram_size"]


def to_global(addr, tile, p=None):
    """本地窗口地址 -> tile 的 SRAM 全局地址；其他地址原样返回。"""
    p = p or params()
    if not is_local(addr, p):
        return addr
    off = addr - local_sram_base(p)
    if off >= tile_sram_size(p):
        raise ValueError(f"本地地址 {addr:#x} 超出 SRAM 容量")
    return tile_sram_base(tile, p) + off
