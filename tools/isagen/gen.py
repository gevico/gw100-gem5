"""
@file  gen.py
@brief 根据 ISA YAML 生成 C++ 头文件和 Python 定义模块

输入是 spec-src/isa/*.yaml，输出供 gem5 侧译码和 Python 汇编器使用。

用法（在仓库根目录执行）：
    .venv/bin/python tools/isagen/gen.py              # 默认使用 spec-src/isa/isa-v0.yaml
    .venv/bin/python tools/isagen/gen.py --check      # 只检查生成结果是否最新，不写文件

@see SPEC-230
"""

import argparse
import sys
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SPEC = REPO_ROOT / "spec-src" / "isa" / "isa-v0.yaml"
CPP_OUT = REPO_ROOT / "gem5-ext" / "func-core" / "include" / "gw100" / "isa_defs.hh"
PY_OUT = REPO_ROOT / "tools" / "gw100_testkit" / "isa_defs.py"

UNITS = {"ctrl": "Ctrl", "dma": "Dma", "me": "Me", "ve": "Ve"}
CPP_TYPES = {"u8": "uint8_t", "u16": "uint16_t", "u32": "uint32_t", "u64": "uint64_t",
             "i32": "int32_t", "f32": "float"}


def camel(name):
    return "".join(part.capitalize() for part in name.lower().split("_"))


def gen_cpp(spec, src_name):
    lines = [
        f"// 由 tools/isagen/gen.py 根据 {src_name} 生成，请勿手改。",
        "#ifndef __GW100_ISA_GEN_ISA_DEFS_HH__",
        "#define __GW100_ISA_GEN_ISA_DEFS_HH__",
        "",
        "#include <cstdint>",
        "",
        "namespace gw100",
        "{",
        "namespace isa",
        "{",
        "",
        f'constexpr const char *kVersion = "{spec["version"]}";',
        f"constexpr unsigned kInstBytes = {spec['instruction_bytes']};",
        f"constexpr uint16_t kLocalTile = {spec['local_tile']:#x};",
        "",
        "enum class Unit : uint8_t",
        "{",
    ]
    for i, u in enumerate(UNITS.values()):
        lines.append(f"    {u} = {i},")
    lines += ["};", "", "enum class Opcode : uint8_t", "{"]
    for op in spec["opcodes"]:
        lines.append(f"    {op['name']} = {op['code']:#04x},")
    lines += ["};", "", "constexpr bool", "isValidOpcode(uint8_t code)", "{",
              "    switch (code) {"]
    for op in spec["opcodes"]:
        lines.append(f"      case {op['code']:#04x}:")
    lines += ["        return true;", "      default:", "        return false;",
              "    }", "}", "", "constexpr Unit", "unitOf(Opcode op)", "{",
              "    switch (op) {"]
    for op in spec["opcodes"]:
        lines.append(f"      case Opcode::{op['name']}: return Unit::{UNITS[op['unit']]};")
    lines += ["    }", "    return Unit::Ctrl;", "}", "", "constexpr bool",
              "isImplemented(Opcode op)", "{", "    switch (op) {"]
    for op in spec["opcodes"]:
        impl = "true" if op["status"] == "implemented" else "false"
        lines.append(f"      case Opcode::{op['name']}: return {impl};")
    lines += ["    }", "    return false;", "}", "", "enum class DType : uint8_t", "{"]
    for name, d in spec["dtypes"].items():
        lines.append(f"    {name} = {d['code']},")
    lines += ["};", "", "/** 元素位宽；INT4 为 4。 */", "constexpr unsigned",
              "dtypeBits(DType t)", "{", "    switch (t) {"]
    for name, d in spec["dtypes"].items():
        lines.append(f"      case DType::{name}: return {d['bits']};")
    lines += ["    }", "    return 0;", "}", "", "constexpr bool", "isValidDType(uint8_t code)",
              "{", f"    return code <= {max(d['code'] for d in spec['dtypes'].values())};",
              "}", "", "/** 头部字段的字节偏移。 */", "namespace hdr", "{"]
    for f in spec["header"]:
        lines.append(f"constexpr unsigned {f['name']} = {f['offset']};  // {f['type']}")
    opnd = spec["operand"]
    lines += ["} // namespace hdr", "", "/** 张量操作数描述符。 */", "namespace opnd", "{",
              f"constexpr unsigned count = {opnd['count']};",
              f"constexpr unsigned base = {opnd['base_offset']};",
              f"constexpr unsigned bytes = {opnd['bytes']};",
              "constexpr unsigned dims = 4;"]
    for f in opnd["fields"]:
        lines.append(f"constexpr unsigned {f['name']} = {f['offset']};  // {f['type']}"
                     + (f" x{f['count']}" if "count" in f else ""))
    lines += ["} // namespace opnd", "", "namespace flags", "{"]
    for name, v in spec["flags"].items():
        lines.append(f"constexpr uint8_t {name} = {v:#04x};")
    lines += ["} // namespace flags", ""]
    for op_name, subs in spec["subops"].items():
        lines += [f"enum class {camel(op_name)}Op : uint8_t", "{"]
        for name, v in subs.items():
            lines.append(f"    {name} = {v},")
        lines += ["};", ""]
    prog = spec["program"]
    lines += ["namespace prog", "{",
              f"constexpr uint32_t magic = {prog['magic']:#010x};",
              f"constexpr unsigned header_bytes = {prog['header_bytes']};",
              f"constexpr unsigned entry_bytes = {prog['entry_bytes']};",
              "} // namespace prog", "", "namespace mgmt_reg", "{"]
    for name, r in spec["mgmt_regs"].items():
        lines.append(f"constexpr unsigned {name} = {r['offset']:#04x};")
    lines += ["constexpr unsigned size = 0x20;", "} // namespace mgmt_reg", ""]
    for rec in ("command_record", "completion_record"):
        ns = "cmd_rec" if rec == "command_record" else "cpl_rec"
        lines += [f"namespace {ns}", "{", "constexpr unsigned bytes = 32;"]
        for f in spec[rec]:
            lines.append(f"constexpr unsigned {f['name']} = {f['offset']};  // {f['type']}")
        lines += [f"}} // namespace {ns}", ""]
    lines += ["} // namespace isa", "} // namespace gw100", "",
              "#endif // __GW100_ISA_GEN_ISA_DEFS_HH__", ""]
    return "\n".join(lines)


def gen_py(spec, src_name):
    out = [
        f'"""由 tools/isagen/gen.py 根据 {src_name} 生成，请勿手改。"""',
        "",
        f"VERSION = {spec['version']!r}",
        f"INST_BYTES = {spec['instruction_bytes']}",
        f"LOCAL_TILE = {spec['local_tile']:#x}",
        "",
        "DTYPES = {",
    ]
    for name, d in spec["dtypes"].items():
        out.append(f"    {name!r}: {{'code': {d['code']}, 'bits': {d['bits']}}},")
    out += ["}", "", "OPCODES = {"]
    for op in spec["opcodes"]:
        out.append(f"    {op['name']!r}: {{'code': {op['code']:#04x}, 'unit': {op['unit']!r}, "
                   f"'status': {op['status']!r}}},")
    out += ["}", "", "SUBOPS = {"]
    for op_name, subs in spec["subops"].items():
        out.append(f"    {op_name!r}: {dict(subs)!r},")
    out += ["}", "", f"FLAGS = {dict(spec['flags'])!r}", "",
            "HEADER = {"]
    for f in spec["header"]:
        out.append(f"    {f['name']!r}: ({f['offset']}, {f['type']!r}),")
    opnd = spec["operand"]
    out += ["}", "",
            f"OPERAND = {{'count': {opnd['count']}, 'base': {opnd['base_offset']}, "
            f"'bytes': {opnd['bytes']}, 'dims': 4}}",
            "OPERAND_FIELDS = {"]
    for f in opnd["fields"]:
        out.append(f"    {f['name']!r}: ({f['offset']}, {f['type']!r}, {f.get('count', 1)}),")
    prog = spec["program"]
    out += ["}", "",
            f"PROGRAM = {{'magic': {prog['magic']:#010x}, 'header_bytes': {prog['header_bytes']}, "
            f"'entry_bytes': {prog['entry_bytes']}}}",
            "MGMT_REGS = {" + ", ".join(f"{k!r}: {v['offset']:#04x}" for k, v in spec["mgmt_regs"].items()) + "}"]
    for rec in ("command_record", "completion_record"):
        name = rec.upper()
        out.append(f"{name} = {{" + ", ".join(f"{f['name']!r}: ({f['offset']}, {f['type']!r})" for f in spec[rec]) + "}")
    out.append("")
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("spec", nargs="?", type=Path, default=DEFAULT_SPEC)
    parser.add_argument("--check", action="store_true", help="只检查生成结果是否最新")
    args = parser.parse_args()

    spec = yaml.safe_load(args.spec.read_text(encoding="utf-8"))
    src_name = args.spec.resolve().relative_to(REPO_ROOT).as_posix()
    outputs = {CPP_OUT: gen_cpp(spec, src_name), PY_OUT: gen_py(spec, src_name)}

    stale = [p for p, text in outputs.items() if not p.exists() or p.read_text(encoding="utf-8") != text]
    if args.check:
        for p in stale:
            print(f"过期：{p.relative_to(REPO_ROOT)}")
        sys.exit(1 if stale else 0)
    for p, text in outputs.items():
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")
        print(f"已生成：{p.relative_to(REPO_ROOT)}")


if __name__ == "__main__":
    main()
