"""
@file  dram_spec.py
@brief 检查 DRAM 来源档案并生成模型与固件共享的已核实定义

本工具只做 G0 的结构、来源、量纲和生成一致性检查，不是 G4 的独立
命令时序检查器或性能参考程序。未知能力不生成数值；G0 未通过时仍可
生成资料目录，但以 GW100_REQUIRE_DRAM_G0 使用头文件会编译失败。

@see spec-dram
"""

import argparse
import hashlib
import json
import re
import sys
from fractions import Fraction
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROFILE = Path("spec-src/dram/h58g66ck8bx147.json")
CLASSIFICATIONS = {
    "required", "sku_unsupported", "analog_excluded", "vendor_unpublished"
}


class SpecError(ValueError):
    """表示档案、资料或生成物不满足 G0 检查要求。"""


def require(condition, message):
    """要求条件成立，否则报告可定位的规格错误。"""
    if not condition:
        raise SpecError(message)


def integer(value, name, minimum=0, maximum=(1 << 64) - 1):
    """检查无符号整数量纲，拒绝把 bool 当成整数。"""
    require(type(value) is int and minimum <= value <= maximum,
            f"{name}: 需要范围 [{minimum}, {maximum}] 内的整数")


def unique_objects(items, key, label):
    """检查对象列表及标识符唯一性。"""
    require(isinstance(items, list) and items, f"{label}: 不得为空")
    seen = set()
    for item in items:
        require(isinstance(item, dict), f"{label}: 条目必须是对象")
        name = item.get(key)
        require(isinstance(name, str) and name, f"{label}: 缺少 {key}")
        require(name not in seen, f"{label}: 重复的 {name}")
        seen.add(name)


def evidence(item, label, page_count, require_verification=True):
    """检查原始页码和人工原页核对状态，不自动批准来源。"""
    pages = item.get("pages")
    require(isinstance(pages, list) and pages, f"{label}: 缺少来源页码")
    for page in pages:
        integer(page, f"{label}.pages", 1, page_count)
    require(len(set(pages)) == len(pages), f"{label}: 来源页码重复")
    if require_verification:
        require(type(item.get("verified")) is bool, f"{label}: 缺少核对状态")


def validate_profile(data):
    """检查来源档案结构和已知组织关系，不判定器件命令是否合法。"""
    require(isinstance(data, dict), "档案必须是 JSON 对象")
    require(type(data.get("schema_version")) is int and
            data["schema_version"] == 1, "不支持的 schema_version")
    require(data.get("sku") == "H58G66CK8BX147", "本工具只接受目标 SKU")
    require(re.fullmatch(r"\d+\.\d+\.\d+", data.get("profile_version", "")),
            "profile_version 必须是三段版本号")
    manual = data.get("manual", {})
    require(isinstance(manual, dict), "manual 必须是对象")
    integer(manual.get("pages"), "manual.pages", 1)
    integer(manual.get("bytes"), "manual.bytes", 1)
    require(re.fullmatch(r"[0-9a-f]{64}", manual.get("sha256", "")),
            "manual.sha256 格式错误")
    require(manual.get("revision") == "1.0", "需要重新审查其他手册版本")
    path = Path(manual.get("path", ""))
    require(not path.is_absolute() and ".." not in path.parts and
            path.parts[:1] == ("reference",), "手册必须位于 reference 内")
    conditions = data.get("conditions")
    require(isinstance(conditions, dict) and
            all(isinstance(v, str) and v for v in conditions.values()),
            "缺少适用条件")
    unique_objects(data.get("constants"), "name", "constants")
    constants = {}
    for item in data["constants"]:
        name = item["name"]
        require(re.fullmatch(r"[A-Z][A-Z0-9_]*", name), f"{name}: 非法常量名")
        integer(item.get("value"), name)
        require(isinstance(item.get("unit"), str) and item["unit"],
                f"{name}: 缺少单位")
        if item.get("origin") == "manual":
            evidence(item, name, manual["pages"])
        else:
            require(item.get("origin") in {"gw100", "derived"} and
                    isinstance(item.get("basis"), str) and item["basis"],
                    f"{name}: 必须区分器件参数、GW100 决策和推算")
        require("condition" not in item or item["condition"] in conditions,
                f"{name}: 未定义的适用条件")
        constants[name] = item["value"]
    try:
        capacity = (constants["PACKAGES"] * constants["CHANNELS_PER_PACKAGE"] *
                    constants["RANKS_PER_CHANNEL"] *
                    constants["RANK_DENSITY_BITS"] // 8)
        require(constants["RANK_DENSITY_BITS"] % 8 == 0 and
                capacity == constants["TOTAL_CAPACITY_BYTES"],
                "容量推算与封装/通道/rank 组织不一致")
        width = constants["CHANNEL_WIDTH_BITS"]
        require(width % 8 == 0 and width > 0, "通道位宽必须是非零整字节")
        interleave = constants["INTERLEAVE_BYTES"]
        require(interleave > 0 and interleave & (interleave - 1) == 0,
                "交织粒度必须是非零的 2 次幂")
        for name in ("BOOT_CK_HZ", "TARGET_CK_HZ", "TARGET_WCK_CK_RATIO"):
            require(constants[name] > 0, f"{name} 必须非零")
    except KeyError as exc:
        raise SpecError(f"缺少组织或时钟常量 {exc}") from exc
    unique_objects(data.get("timings"), "name", "timings")
    for item in data["timings"]:
        name = item["name"]
        require(re.fullmatch(r"[A-Z][A-Z0-9_]*", name), f"{name}: 非法时序名")
        require(item.get("kind") in {"max", "max_minus_ck"},
                f"{name}: 未知时序公式")
        integer(item.get("ps"), f"{name}.ps")
        integer(item.get("ck"), f"{name}.ck")
        require(item["ps"] or item["ck"], f"{name}: 时序不得全零")
        if item["kind"] == "max_minus_ck":
            integer(item.get("subtract_ck"), f"{name}.subtract_ck", 1)
            require(item["subtract_ck"] <= item["ck"],
                    f"{name}: 减项可能产生负等待时间")
        require(item.get("condition") in conditions, f"{name}: 缺少适用条件")
        require(isinstance(item.get("anchor"), str) and item["anchor"],
                f"{name}: 缺少命令时间锚点")
        evidence(item, name, manual["pages"])
        require(not any(527 <= p <= 530 for p in item["pages"]),
                f"{name}: LPDDR5 表不能作为 LPDDR5X 参数依据")
    defaults = data.get("mr_defaults")
    require(isinstance(defaults, list) and defaults, "缺少 MR 掩码默认值")
    masks = {}
    for item in defaults:
        require(isinstance(item, dict), "MR 默认值必须是对象")
        integer(item.get("mr"), "mr_defaults.mr", 0, 127)
        integer(item.get("mask"), "mr_defaults.mask", 1, 255)
        integer(item.get("value"), "mr_defaults.value", 0, 255)
        require(item["value"] & ~item["mask"] == 0, "MR 默认值超出已知掩码")
        mr = item["mr"]
        require(mr not in masks, f"MR{mr} 默认掩码须合并为一项")
        masks[mr] = masks.get(mr, 0) | item["mask"]
        evidence(item, f"MR{mr} 默认值", manual["pages"])
    unique_objects(data.get("capabilities"), "id", "capabilities")
    for item in data["capabilities"]:
        name = item["id"]
        require(re.fullmatch(r"[a-z][a-z0-9_]*", name), f"{name}: 非法能力名")
        require("value" in item, f"{name}: 未知能力须显式使用 null")
        evidence(item, name, manual["pages"])
        if "mr" in item:
            integer(item["mr"], f"{name}.mr", 0, 127)
            integer(item.get("mask"), f"{name}.mask", 1, 255)
        if item["value"] is None:
            require(item["verified"] is False and item.get("missing"),
                    f"{name}: 未知能力不能标为已核实，须说明缺失证据")
        else:
            integer(item["value"], f"{name}.value", 0, item.get("mask", 255))
            require(item.get("basis"), f"{name}: 缺少型号取值依据")
            if "mask" in item:
                require(item["value"] & ~item["mask"] == 0,
                        f"{name}: 能力值超出寄存器掩码")
    unique_objects(data.get("conflicts"), "id", "conflicts")
    for item in data["conflicts"]:
        require(item.get("status") in {"open", "resolved"},
                f"{item['id']}: 冲突状态非法")
        evidence(item, item["id"], manual["pages"], False)
        require(item.get("description") and item.get("required_evidence"),
                f"{item['id']}: 缺少冲突说明")
        require(item["status"] == "open" or item.get("resolution"),
                f"{item['id']}: 未给出解决证据")
    unique_objects(data.get("coverage"), "id", "coverage")
    for item in data["coverage"]:
        require(item.get("classification") in CLASSIFICATIONS,
                f"{item['id']}: 未分类的功能")
        evidence(item, item["id"], manual["pages"], False)
        for key in ("behavior", "implementation", "test"):
            require(item.get(key), f"{item['id']}: 缺少 {key}")
        if item["classification"] == "required":
            require(item["implementation"].startswith("planned:") and
                    item["test"].startswith("planned:"),
                    "当前档案只有 G0 清单，不能把路径存在当作实现验收")
    review = data.get("review", {})
    require(review.get("status") in {"pending", "passed"}, "缺少交叉审查状态")
    for key in ("field_coverage_complete", "full_timing_table_complete",
                "interface_complete"):
        require(type(review.get(key)) is bool, f"review.{key}: 需要布尔值")
    mmio = data.get("mmio")
    require(isinstance(mmio, dict), "缺少 Memory Tile MMIO 定义")
    for key in ("version", "tile_register_offset", "channel_base",
                "channel_stride", "protection_base", "protection_stride",
                "protection_entries"):
        integer(mmio.get(key), f"mmio.{key}", 1, (1 << 32) - 1)
    require(mmio["channel_stride"] % 8 == 0 and
            mmio["channel_base"] % 8 == 0, "通道块必须 64 位对齐")
    common_end = mmio["protection_base"]
    protection_end = (common_end + mmio["protection_stride"] *
                      mmio["protection_entries"])
    require(protection_end <= mmio["channel_base"], "保护表与通道块重叠")
    require(mmio["protection_stride"] >= 20 and
            mmio["protection_stride"] % 8 == 0, "保护项大小或对齐错误")
    require(mmio["tile_register_offset"] + mmio["channel_base"] +
            constants["CHANNELS_PER_PACKAGE"] * mmio["channel_stride"] <=
            16 * 1024 * 1024, "MMIO 越出 Tile 窗口")
    for block, limit in (("common", common_end),
                         ("channel", mmio["channel_stride"])):
        unique_objects(mmio.get(block), "name", f"mmio.{block}")
        occupied = set()
        for reg in mmio[block]:
            require(re.fullmatch(r"[A-Z][A-Z0-9_]*", reg["name"]),
                    "非法 MMIO 寄存器名")
            require(reg.get("width") in {32, 64}, "寄存器宽度只能为 32/64")
            integer(reg.get("offset"), "MMIO offset", 0, limit - 1)
            size = reg["width"] // 8
            require(reg["offset"] % size == 0 and
                    reg["offset"] + size <= limit, "寄存器越界或未对齐")
            require(reg.get("access") in {"RO", "RW", "W1C", "W1S"},
                    "未定义的寄存器访问类型")
            span = set(range(reg["offset"], reg["offset"] + size))
            require(not occupied & span, "MMIO 寄存器重叠")
            occupied.update(span)


def verify_manual(data, root):
    """核对本地原件的大小和 SHA-256，不替代人工原页检查。"""
    path = root / data["manual"]["path"]
    require(path.is_file(), f"未找到原件：{path}；需要 reference 子模块")
    require(path.stat().st_size == data["manual"]["bytes"], "原件大小发生变化")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    require(digest == data["manual"]["sha256"], "原件 SHA-256 不匹配")


def blockers(data):
    """由证据状态推导 G0 阻塞，不能通过手改一个总开关放行。"""
    result = []
    for group in ("constants", "timings", "mr_defaults", "capabilities"):
        for item in data[group]:
            if item.get("value", 0) is None or item.get("verified", True) is False:
                name = item.get("name", item.get("id", f"MR{item.get('mr')}"))
                result.append(f"{group}/{name}: {item.get('missing', '原页核对未完成')}")
    for item in data["conflicts"]:
        if item["status"] != "resolved":
            result.append(f"conflict/{item['id']}: {item['description']}")
    for key in ("field_coverage_complete", "full_timing_table_complete",
                "interface_complete"):
        if not data["review"][key]:
            result.append(f"review/{key}: 未完成")
    if data["review"]["status"] != "passed":
        result.append("review/cross_review: 尚无通过记录")
    return result


def source_label(item):
    """给每个已知定义保留适用条件与依据。"""
    if "pages" in item:
        label = "SK1 p." + ",".join(map(str, item["pages"]))
    else:
        label = item["origin"] + ": " + item["basis"]
    if "condition" in item:
        label += "; " + item["condition"]
    if "unit" in item:
        label += "; " + item["unit"]
    return label


def definitions(data):
    """仅导出已核对数值；未知能力和默认值掩码外的位不补零。"""
    for item in data["constants"]:
        if item.get("verified", True):
            yield item["name"], item["value"], source_label(item)
    for item in data["timings"]:
        if item["verified"]:
            for key, suffix in (("ps", "MIN_PS"), ("ck", "MIN_CK"),
                                ("subtract_ck", "SUBTRACT_CK")):
                if key in item:
                    yield item["name"] + "_" + suffix, item[key], source_label(item)
    for item in data["mr_defaults"]:
        if item["verified"]:
            stem = f"MR{item['mr']}_DEFAULT"
            yield stem + "_MASK", item["mask"], source_label(item)
            yield stem + "_VALUE", item["value"], source_label(item)
    for item in data["capabilities"]:
        if item["value"] is not None and item["verified"]:
            yield "CAP_" + item["id"].upper(), item["value"], source_label(item)
    for name, value in data["mmio"].items():
        if type(value) is int:
            yield "MMIO_" + name.upper(), value, "GW100 控制器设计；spec-dram §7"
    for block in ("common", "channel"):
        for reg in data["mmio"][block]:
            yield ("REG_" + block.upper() + "_" + reg["name"], reg["offset"],
                   f"GW100 控制器设计；{reg['access']} {reg['width']} bit；spec-dram §7")


def coverage_markdown(data, digest):
    """生成来源覆盖清单，明确所有实现和测试仍为 planned。"""
    rows = [
        "# spec-dram 来源覆盖清单",
        "",
        "<!-- 请勿手改；由 tools/dram_spec.py 生成 -->",
        "",
        f"档案版本：{data['profile_version']}；档案 SHA-256：{digest}。",
        "",
        "这是功能组级别的 G0 来源清单，尚未完成逐 MR 位域和逐命令约束展开。",
        "下表的 planned: 是后续实现/测试落点，路径和用例名不表示已经存在或通过。",
        "精确模型、固件初始化、独立时序及性能验收均未交付。",
        "",
        "| 功能 | 分类 | SK1 PDF 页 | 数字行为/边界 | 实现落点 | 测试落点 |",
        "|---|---|---|---|---|---|",
    ]
    for item in data["coverage"]:
        rows.append("| " + " | ".join([
            item["id"], item["classification"], ", ".join(map(str, item["pages"])),
            item["behavior"], item["implementation"], item["test"]
        ]) + " |")
    rows.extend([
        "", "## G0 阻塞", "",
        "来源或审查状态缺失时不能进入 G1。以下项目由档案自动推导：", ""
    ])
    rows.extend("- " + item for item in blockers(data))
    rows.extend([
        "", "未知只读能力不作为“厂商私有行为”排除：编码和影响已公开，缺的是型号取值。",
        "只有明确标为 vendor specific、且未公开语义的部分属于 vendor_unpublished。",
        ""
    ])
    return "\n".join(rows)


def generated_files(data, profile_bytes):
    """生成纯定义文件，未通过的 G0 不能被精确配置误当作已就绪。"""
    digest = hashlib.sha256(profile_bytes).hexdigest()
    ready = int(not blockers(data))
    items = list(definitions(data))
    stamp = f"源：{PROFILE.as_posix()} SHA-256={digest}"
    c = [
        "/**", " * @file  gw100_dram_defs.h",
        " * @brief DRAM 已核实定义；请勿手改",
        f" * {stamp}", " * @see spec-dram", " */",
        "#ifndef GW100_DRAM_DEFS_H", "#define GW100_DRAM_DEFS_H",
        "#include <stdint.h>", "",
        f"#define GW100_DRAM_SPEC_G0_READY {ready}",
        "#if defined(GW100_REQUIRE_DRAM_G0) && !GW100_DRAM_SPEC_G0_READY",
        '#error "SK hynix G0 source/review gate has not passed"',
        "#endif", "",
    ]
    cc = [
        "/**", " * @file  sk_hynix_defs.hh",
        " * @brief DRAM 已核实定义；请勿手改",
        f" * {stamp}", " * @see spec-dram", " */",
        "#ifndef GW100_SK_HYNIX_DEFS_HH", "#define GW100_SK_HYNIX_DEFS_HH",
        "#include <cstdint>", "",
        "namespace gem5::gw100::dram {",
        f"inline constexpr bool SpecG0Ready = {'true' if ready else 'false'};",
        "#ifdef GW100_REQUIRE_DRAM_G0",
        'static_assert(SpecG0Ready, "SK hynix G0 source/review gate has not passed");',
        "#endif", "",
    ]
    py = [
        '"""', "@file  sk_hynix_defs.py",
        "@brief DRAM 已核实定义；请勿手改",
        stamp, "@see spec-dram", '"""', "",
        f"SPEC_G0_READY = {bool(ready)!r}",
        f"PROFILE_SHA256 = {digest!r}", "",
    ]
    for name, value, label in items:
        c.extend([f"/* {label} */", f"#define GW100_DRAM_{name} UINT64_C({value})"])
        cc.extend([f"// {label}", f"inline constexpr std::uint64_t {name} = {value}ULL;"])
        py.extend([f"# {label}", f"{name} = {value}"])
    c.extend(["", "#endif", ""])
    cc.extend(["", "} // namespace gem5::gw100::dram", "", "#endif", ""])
    py.append("")
    return {
        Path("firmware/common/generated/gw100_dram_defs.h"): "\n".join(c),
        Path("gem5-ext/src/mem-tile/generated/sk_hynix_defs.hh"): "\n".join(cc),
        Path("gem5-ext/configs/gw100/generated/sk_hynix_defs.py"): "\n".join(py),
        Path("docs/spec/memory/spec-dram/coverage.md"): coverage_markdown(data, digest),
    }


def reject_duplicate_keys(pairs):
    """拒绝 JSON 重复键，避免后一个值悄悄覆盖已审查定义。"""
    result = {}
    for key, value in pairs:
        require(key not in result, f"JSON 重复键：{key}")
        result[key] = value
    return result


def load_profile(path):
    """读取 UTF-8 档案并保留原始字节供生成物摘要使用。"""
    content = path.read_bytes()
    data = json.loads(content, object_pairs_hook=reject_duplicate_keys)
    validate_profile(data)
    return data, content


def main(argv=None):
    """执行生成或只读一致性检查；资料阻塞以独立退出码报告。"""
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--generate", action="store_true", help="更新共享定义和覆盖清单")
    action.add_argument("--check", action="store_true", help="检查来源和生成物，无写入")
    parser.add_argument("--require-g0", action="store_true",
                        help="G0 阻塞时返回 2；--check 成功不等于 G0 通过")
    args = parser.parse_args(argv)
    try:
        data, content = load_profile(ROOT / PROFILE)
        verify_manual(data, ROOT)
        for path, expected in generated_files(data, content).items():
            target = ROOT / path
            if args.generate:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(expected, encoding="utf-8")
            else:
                require(target.is_file() and
                        target.read_text(encoding="utf-8") == expected,
                        f"生成物过期或缺失：{path}；运行 --generate")
        constants = {i["name"]: i["value"] for i in data["constants"]}
        actual = (constants["TARGET_CK_HZ"] *
                  constants["TARGET_WCK_CK_RATIO"] * 2)
        period = Fraction(10**12, constants["TARGET_CK_HZ"])
        print(f"原件、档案和共享定义检查通过；标称 8533，实际 {actual / 10**6:g} Mbps")
        print(f"CK 周期 = {period.numerator}/{period.denominator} ps；"
              "本工具未运行命令时序或性能验收")
        pending = blockers(data)
        print(f"G0: {'BLOCKED' if pending else 'PASS'} ({len(pending)} 项)")
        for item in pending:
            print("  " + item)
        return 2 if args.require_g0 and pending else 0
    except (SpecError, OSError, ValueError, TypeError, KeyError) as exc:
        print(f"DRAM 规格检查失败：{exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
