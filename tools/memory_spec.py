"""
@file  memory_spec.py
@brief 生成通用 Memory Tile 的固件、模型和配置共享定义

数值、寄存器和枚举由通用档案拥有，器件时序仍由 LPDDR5.py 拥有。
--check 验证生成一致性，适合构建和提交检查。

@see spec-dram
"""

import argparse
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "spec-src/dram/gw100-memory.json"


def definitions(data):
    """校验量纲、编码唯一性和物理容量，返回有序常量。"""
    values = {}
    for group, items in data["definitions"].items():
        codes = set()
        for item in items:
            name, value = item["name"], item["value"]
            if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
                raise ValueError(f"非法名称 {name}")
            if (name in values or type(value) is not int or
                    not 0 <= value < 2**64):
                raise ValueError(f"重复名称或越界数值 {name}")
            if not item["unit"] or not item["basis"]:
                raise ValueError(f"缺少量纲或依据 {name}")
            if group.startswith(("register_", "enum_")) and value in codes:
                raise ValueError(f"重复编码 {group} {value}")
            values[name] = value
            codes.add(value)
    v = values
    capacity = (v["TILES"] * v["CHANNELS_PER_TILE"] *
                v["RANKS_PER_CHANNEL"] * v["DEVICE_BYTES"])
    if capacity != v["TOTAL_BYTES"] or capacity != 32 << 30:
        raise ValueError("组织与 32 GiB 容量不一致")
    if v["CHANNELS"] != v["TILES"] * v["CHANNELS_PER_TILE"]:
        raise ValueError("通道数量不一致")
    if v["CK_PS"] != round(8_000_000 / v["DATA_RATE_MT_S"]):
        raise ValueError("CK 量化与数据率不一致")
    for name in ("INTERLEAVE_BYTES", "BURST_BYTES", "FIFO_DEPTH"):
        if v[name] == 0 or v[name] & (v[name] - 1):
            raise ValueError(f"需要非零二次幂 {name}")
    if v["TREFI_PS"] <= v["TRFC_PS"]:
        raise ValueError("刷新间隔必须大于刷新时长")
    if v["TRAIN_PS"] >= v["TIMEOUT_PS"]:
        raise ValueError("训练时延越过阶段超时")
    return values


def outputs(data, raw):
    """生成三端共用的定义，嵌入来源摘要。"""
    values = definitions(data)
    digest = hashlib.sha256(raw).hexdigest()
    stamp = f"gw100-memory.json SHA-256={digest}"
    c = ["/**", " * @file  gw100_memory_defs.h",
         " * @brief 通用内存共享定义；请勿手改", f" * {stamp}",
         " * @see spec-dram", " */", "#ifndef GW100_MEMORY_DEFS_H",
         "#define GW100_MEMORY_DEFS_H", "#include <stdint.h>", ""]
    cc = ["/**", " * @file  memory_defs.hh",
          " * @brief 通用内存共享定义；请勿手改", f" * {stamp}",
          " * @see spec-dram", " */", "#ifndef GW100_MEMORY_DEFS_HH",
          "#define GW100_MEMORY_DEFS_HH", "#include <cstdint>", "",
          "namespace gem5::gw100::memorydefs {", ""]
    py = ['"""', "@file  memory_defs.py",
          "@brief 通用内存共享定义；请勿手改", stamp,
          "@see spec-dram", '"""', ""]
    for name, value in values.items():
        c.append(f"#define GW_MEM_{name} UINT64_C({value})")
        cc.append(f"inline constexpr uint64_t {name} = {value}ULL;")
        py.append(f"{name} = {value}")
    c.extend(["", "#endif", ""])
    cc.extend(["", "} // namespace gem5::gw100::memorydefs", "#endif", ""])
    py.append("")
    return {
        "firmware/common/generated/gw100_memory_defs.h": "\n".join(c),
        "gem5-ext/src/gw100/mem_tile/generated/memory_defs.hh": "\n".join(cc),
        "gem5-ext/configs/gw100/generated/memory_defs.py": "\n".join(py),
    }


def main():
    """生成共享定义，或在文件不一致时返回失败。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    raw = PROFILE.read_bytes()
    for name, content in outputs(json.loads(raw), raw).items():
        path = ROOT / name
        if args.check:
            if not path.exists() or path.read_text() != content:
                parser.exit(1, f"共享定义需要重新生成 {name}\n")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    print("Memory Tile 共享定义检查通过")


if __name__ == "__main__":
    main()
