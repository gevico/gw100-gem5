"""
@file  sram_spec.py
@brief 校验 SRAM 档案并生成三端共享定义
@see spec-sram
"""

import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "spec-src/sram/gw100-sram.json"
FIELDS = {
    "size",
    "banks",
    "word_bytes",
    "interleaved",
    "split_ports",
    "sources",
    "slots",
    "read_cycles",
    "write_cycles",
    "interval_cycles",
    "mask_bytes",
    "ecc",
    "period_ps",
    "boot_period_ps",
}


def validate_layout(size, firmware, rom, crt):
    """核对固定管理 ABI 的装载区、运行区、ROM 保留页与栈顶。"""

    def region(script, name):
        match = re.search(
            r"\b" + name + r"\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*"
            r"(0x[0-9A-Fa-f]+|\d+)\s*,\s*LENGTH\s*=\s*([^\n}]+)",
            script,
        )
        if not match:
            raise ValueError(f"链接区域缺失 {name}")
        expression = re.sub(r"\s", "", match[2])
        number = r"(?:0x[0-9A-Fa-f]+|\d+)[KM]?"
        if not re.fullmatch(number + r"(?:-" + number + r")*", expression):
            raise ValueError(f"链接长度表达式不支持 {name}")
        values = []
        for term in expression.split("-"):
            scale = {"K": 1024, "M": 1024**2}.get(term[-1], 1)
            values.append(int(term[:-1] if scale != 1 else term, 0) * scale)
        return int(match[1], 0), values[0] - sum(values[1:])

    base, top = 0x10000000, 0x10000000 + size
    if (
        region(firmware, "LOAD") != (base, size // 2)
        or region(firmware, "RUN") != (base + size // 2, size // 2 - 4096)
        or region(rom, "RSV") != (top - 4096, 4096)
        or not re.search(r"\bli\s+sp\s*,\s*" + hex(top) + r"\b", crt)
    ):
        raise ValueError("管理 SRAM 与固件链接布局不匹配")


def validate(data):
    """检查物理组织、有限容量与管理布局。"""
    if data["version"] != 1 or not data["basis"]:
        raise ValueError("缺少版本或依据")
    if data["limits"] != {
        "max_request_bytes": 64,
        "ecc_data_bytes": 8,
        "ecc_check_bits": 8,
        "max_bank_wait_cycles": 16,
    }:
        raise ValueError("不支持的协议范围")
    if set(data["profiles"]) != {"management", "compute_1rw", "compute_1r1w"}:
        raise ValueError("首版档案集合不完整")
    for name, p in data["profiles"].items():
        if set(p) != FIELDS:
            raise ValueError(f"档案字段不完整 {name}")
        for key, value in p.items():
            expected = (
                bool if key in {"interleaved", "split_ports", "ecc"} else int
            )
            if type(value) is not expected or (expected is int and value <= 0):
                raise ValueError(f"非法参数 {name}.{key}")
        for key in ("banks", "word_bytes", "mask_bytes"):
            if p[key] & (p[key] - 1):
                raise ValueError(f"需要二的幂 {name}.{key}")
        if (
            p["word_bytes"] not in (8, 16, 32, 64)
            or p["mask_bytes"] > p["word_bytes"]
            or p["size"] % (p["banks"] * p["word_bytes"])
        ):
            raise ValueError(f"非法阵列组织 {name}")
        if p["sources"] > 64 or p["slots"] > 256:
            raise ValueError(f"逻辑资源超出实现范围 {name}")
    m = data["profiles"]["management"]
    # 固件链接脚本与栈顶属于管理 ABI，其他容量在独立夹具中探索。
    if m["size"] != 2 << 20:
        raise ValueError("管理容量与 ROM 栈顶不匹配")
    validate_layout(
        m["size"],
        (ROOT / "firmware/fw/fw.ld").read_text(),
        (ROOT / "firmware/rom/rom.ld").read_text(),
        (ROOT / "firmware/rom/crt0.S").read_text(),
    )
    bound = m["sources"] * max(
        m["read_cycles"] + m["write_cycles"], 2 * m["interval_cycles"]
    )
    if bound > data["limits"]["max_bank_wait_cycles"]:
        raise ValueError("管理档案不能满足 bank 仲裁保证")


def outputs(data, raw):
    """源档案唯一拥有常量。"""
    validate(data)
    digest = hashlib.sha256(raw).hexdigest()
    values = {key.upper(): value for key, value in data["limits"].items()}
    for name, profile in data["profiles"].items():
        values.update(
            {
                f"{name.upper()}_{key.upper()}": int(value)
                for key, value in profile.items()
            }
        )
    header = f"gw100-sram.json SHA-256={digest}"
    c = [
        "/**",
        " * @file  gw100_sram_defs.h",
        " * @brief SRAM 自动生成的共享定义",
        f" * {header}",
        " * @see spec-sram",
        " */",
        "#pragma once",
        "",
    ]
    cc = [
        "/**",
        " * @file  sram_defs.hh",
        " * @brief SRAM 自动生成的共享定义",
        f" * {header}",
        " * @see spec-sram",
        " */",
        "#pragma once",
        "#include <cstdint>",
        "namespace gem5::gw100::sramdefs {",
        "",
    ]
    py = [
        '"""',
        "@file  sram_defs.py",
        "@brief SRAM 自动生成的共享定义",
        header,
        "@see spec-sram",
        '"""',
        "",
    ]
    for name, value in values.items():
        c.append(f"#define GW_SRAM_{name} {value}ULL")
        cc.append(f"inline constexpr uint64_t {name} = {value}ULL;")
        py.append(f"{name} = {value}")
    cc.extend(["}", ""])
    return {
        ROOT / "firmware/common/generated/gw100_sram_defs.h": "\n".join(c)
        + "\n",
        ROOT / "gem5-ext/src/gw100/sram/generated/sram_defs.hh": "\n".join(cc),
        ROOT / "gem5-ext/configs/gw100/generated/sram_defs.py": "\n".join(py)
        + "\n",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    raw = PROFILE.read_bytes()
    try:
        generated = outputs(json.loads(raw), raw)
    except (ValueError, KeyError) as exc:
        parser.error(str(exc))
    for path, content in generated.items():
        if args.check:
            if not path.exists() or path.read_text() != content:
                raise SystemExit(f"生成物不同步 {path.relative_to(ROOT)}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    print("SRAM 共享定义 PASS")


if __name__ == "__main__":
    main()
