"""
@file  pcie_spec.py
@brief 生成 PCIe 链路共享定义并检查来源与实施关口

档案分别记录协议依据和参考微架构预算。生成一致性检查和关口验收
使用独立入口，--gate 在资料或实施证据不足时返回 2。

@see spec-pcie
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "spec-src/pcie/gw100-link.json"
REQUIRED_FACTS = {
    "RATE",
    "CREDIT_UNIT",
    "RCB",
    "RESOURCES",
    "BOUNDARIES",
    "TIMERS",
    "FRAMING",
    "CRC",
    "FLOW_CONTROL",
    "WIRE_FIELDS",
    "ORDERING",
    "LTSSM",
    "L1",
    "CONFIG_RESET",
    "ERROR_RESET",
}
DESIGN_FACTS = {"RESOURCES", "BOUNDARIES"}
PROTOCOL_TABLES = {
    "ack_replay_limits",
    "framing_rules",
    "crc_rules",
    "fc_state_machine",
    "wire_fields",
    "ordering_matrix",
    "ltssm_rules",
    "l1_rules",
    "configuration_reset_values",
    "error_reset_rules",
}
OFFICIAL_HOSTS = {
    "pcisig.com",
    "docs.amd.com",
    "www.amd.com",
    "www.intel.com",
    "cdrdv2-public.intel.com",
    "docs.altera.com",
    "www.latticesemi.com",
}


def validate_source(source):
    """核对官方来源或用户允许的原版规范镜像及其固定原件信息。"""
    identifier = source["id"]
    url = urlparse(source["url"])
    if url.scheme != "https":
        raise ValueError(f"需要 HTTPS 来源 {identifier}")
    if "artifact_sha256" in source:
        if not re.fullmatch(r"[0-9a-f]{64}", source["artifact_sha256"]):
            raise ValueError(f"非法原件摘要 {identifier}")
    if source.get("kind", "official") == "official":
        if url.hostname not in OFFICIAL_HOSTS:
            raise ValueError(f"需要公开官方来源 {identifier}")
        return
    if source["kind"] != "original_spec_mirror":
        raise ValueError(f"未知来源类型 {identifier}")
    required = (
        "artifact_sha256",
        "artifact_bytes",
        "publisher",
        "document_date",
        "revision_check",
        "official_catalog_url",
        "mirror_authorization",
    )
    if any(not source.get(name) for name in required):
        raise ValueError(f"原版规范镜像缺少溯源信息 {identifier}")
    if (
        source["publisher"] != "PCI-SIG"
        or source["mirror_authorization"] != "user-2026-11-07"
    ):
        raise ValueError(f"原版规范镜像范围不符 {identifier}")
    catalog = urlparse(source["official_catalog_url"])
    if catalog.scheme != "https" or catalog.hostname != "pcisig.com":
        raise ValueError(f"原版规范缺少官方书目对应 {identifier}")
    if (
        type(source["artifact_bytes"]) is not int
        or source["artifact_bytes"] <= 0
    ):
        raise ValueError(f"非法原件字节数 {identifier}")
    if url.hostname == "raw.githubusercontent.com":
        commit = source.get("mirror_commit", "")
        if not re.fullmatch(
            r"[0-9a-f]{40}", commit
        ) or commit not in url.path.split("/"):
            raise ValueError(f"GitHub 镜像需要固定提交 {identifier}")


def validate(data):
    """检查档案结构、来源归属和资源约束，返回共享常量。"""
    if data["schema_version"] != 1 or data["profile"] != "gw100-pcie-link":
        raise ValueError("不支持的 PCIe 档案格式")
    scope = data["scope"]
    expected = {
        "generations": [1, 2, 3, 4],
        "widths": [1, 2, 4],
        "mps_bytes": [128, 256],
        "mrrs_bytes": [128, 256, 512],
        "rcb_bytes": [64, 128],
        "completion_payload_bytes": [64, 128, 256],
    }
    for name, values in expected.items():
        if scope[name] != values:
            raise ValueError(f"首版能力集合不一致 {name}")
    if scope["pf_count"] != 1 or scope["vc_count"] != 1:
        raise ValueError("首版只定义一个 PF 和 VC0")
    sources = {}
    for source in data["sources"]:
        identifier = source["id"]
        if identifier in sources:
            raise ValueError(f"重复来源 {identifier}")
        validate_source(source)
        for name in ("title", "version", "locator", "review", "reviewed_on"):
            if not source[name]:
                raise ValueError(f"来源缺少 {name} {identifier}")
        sources[identifier] = source
    facts = {}
    required_scope = [
        f"Gen{generation}" for generation in scope["generations"]
    ]
    for item in data["coverage"]:
        identifier = item["id"]
        if identifier in facts:
            raise ValueError(f"重复覆盖项 {identifier}")
        if item["required_scope"] != required_scope:
            raise ValueError(f"覆盖范围缩小 {identifier}")
        if not set(item["verified_scope"]).issubset(required_scope):
            raise ValueError(f"非法核验范围 {identifier}")
        status = item["status"]
        if status not in {"verified", "design", "partial", "blocked"}:
            raise ValueError(f"非法依据状态 {identifier}")
        if status == "design" and identifier not in DESIGN_FACTS:
            raise ValueError(f"协议事实不能改为设计假设 {identifier}")
        if status in {"verified", "design"}:
            if item["missing"] or item["verified_scope"] != required_scope:
                raise ValueError(f"完成状态与未核验范围冲突 {identifier}")
        elif not item["missing"]:
            raise ValueError(f"资料缺口缺少具体说明 {identifier}")
        if status == "verified" and not item["evidence"]:
            raise ValueError(f"核验项缺少来源 {identifier}")
        for evidence in item["evidence"]:
            if evidence not in sources or evidence == "SIG-CATALOG":
                raise ValueError(f"缺少可用协议依据 {identifier} {evidence}")
        if not item["planned_implementation"] or not item["planned_tests"]:
            raise ValueError(f"覆盖项缺少实现或验证落点 {identifier}")
        facts[identifier] = item
    if set(facts) != REQUIRED_FACTS:
        raise ValueError("必要协议覆盖项缺失或出现未分类项")
    if set(data["protocol_values"]) != PROTOCOL_TABLES:
        raise ValueError("必要协议规则表缺失")
    for name, value in data["protocol_values"].items():
        if value is not None and not isinstance(value, (dict, list)):
            raise ValueError(f"协议规则表需要结构化内容 {name}")
    validate_protocol_tables(data)
    values = {}
    for item in data["definitions"]:
        name, value = item["name"], item["value"]
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name) or name in values:
            raise ValueError(f"重复或非法常量 {name}")
        if type(value) is not int or not 0 <= value < 2**64:
            raise ValueError(f"常量需要 uint64 数值 {name}")
        fact = facts.get(item["fact"])
        if not fact or fact["status"] not in {"verified", "design"}:
            raise ValueError(f"常量依赖未收敛的依据 {name}")
        if not item["unit"] or not item["basis"]:
            raise ValueError(f"常量缺少单位或依据 {name}")
        values[name] = value
    v = values
    if v["DEFAULT_GENERATION"] not in scope["generations"]:
        raise ValueError("默认代际超出能力集合")
    if v["DEFAULT_WIDTH"] not in scope["widths"]:
        raise ValueError("默认宽度超出能力集合")
    if v["CONTROLLER_PERIOD_PS"] != 4000 or v["TICK_PS"] != 1:
        raise ValueError("首版控制器时钟或全局 tick 不一致")
    if v["CONTROLLER_DATAPATH_BYTES"] != 32:
        raise ValueError("首版控制器数据通路应为 256 位")
    if v["DATA_CREDIT_BYTES"] != 16:
        raise ValueError("协议数据信用单位应为 16 B")
    for name in (
        "RX_PD_BYTES",
        "RX_NPD_BYTES",
        "TX_PD_BYTES",
        "TX_NPD_BYTES",
        "TX_CPLD_BYTES",
    ):
        if v[name] == 0 or v[name] % v["DATA_CREDIT_BYTES"]:
            raise ValueError(f"数据预算需要完整信用单位 {name}")
    if v["MAX_PAYLOAD_BYTES"] != max(scope["mps_bytes"]):
        raise ValueError("MPS 能力与资源档案不一致")
    if v["MAX_READ_REQUEST_BYTES"] != max(scope["mrrs_bytes"]):
        raise ValueError("MRRS 能力与资源档案不一致")
    if (v["MIN_RCB_BYTES"], v["MAX_RCB_BYTES"]) != (64, 128):
        raise ValueError("RCB 能力与资源档案不一致")
    if v["COMPLETION_DATA_BYTES"] < (
        v["MAX_OUTSTANDING_READS"] * v["MAX_READ_REQUEST_BYTES"]
    ):
        raise ValueError("完成数据预留不足")
    rcb = v["MIN_RCB_BYTES"]
    worst_headers = (v["MAX_READ_REQUEST_BYTES"] + 2 * rcb - 2) // rcb
    if v["COMPLETION_HEADER_SLOTS"] < (
        v["MAX_OUTSTANDING_READS"] * worst_headers
    ):
        raise ValueError("未对齐请求的完成头预留不足")
    alignment = v["REPLAY_ALIGNMENT_BYTES"]
    if alignment == 0 or alignment & (alignment - 1):
        raise ValueError("replay 存储对齐需要非零二次幂")
    if v["REPLAY_DATA_BYTES"] < v["MAX_PAYLOAD_BYTES"]:
        raise ValueError("replay 数据预算不足以容纳最大载荷")
    for name in (
        "RX_PH",
        "RX_NPH",
        "REPLAY_DESCRIPTOR_SLOTS",
        "REPLAY_DESCRIPTOR_BYTES",
        "TX_PH",
        "TX_NPH",
        "TX_CPLH",
        "TX_DLLP_SLOTS",
        "BACKEND_SLOTS",
        "HOST_PACKET_SLOTS",
    ):
        if v[name] == 0:
            raise ValueError(f"有限资源需要非零容量 {name}")
    if v["RX_NPD_BYTES"] < v["RX_NPH"] * v["DATA_CREDIT_BYTES"]:
        raise ValueError("配置写的 non-posted 数据预算不足")
    if not 0 < v["RC_POSTED_RESERVED_SLOTS"] < v["BACKEND_SLOTS"]:
        raise ValueError("RC posted 预留需要保留有限 NP 后端容量")
    if v["BENCH_WINDOW_PS"] != 100_000_000:
        raise ValueError("测量窗口需要为 100 µs")
    if v["ACK_BATCH_TLPS"] != 8 or v["CREDIT_RELEASE_CYCLES"] != 2:
        raise ValueError("首版 ACK 或信用释放策略不一致")
    if v["ASPM_L1_SUPPORTED"] != 1 or v["ASPM_L1_DEFAULT_ENABLED"] != 0:
        raise ValueError("首版 ASPM 策略不一致")
    for index, milestone in enumerate(data["milestones"]):
        if milestone["id"] != f"G{index}":
            raise ValueError("实施关口顺序不一致")
        expected_dependency = [] if index == 0 else [f"G{index - 1}"]
        if milestone["depends_on"] != expected_dependency:
            raise ValueError("实施关口依赖不一致")
        if milestone["implementation"] not in {
            "pending",
            "partial",
            "complete",
        }:
            raise ValueError("非法实施状态")
    if len(data["milestones"]) != 4:
        raise ValueError("缺少实施关口")
    crc_rules = data["protocol_values"]["crc_rules"]
    if crc_rules:
        for index, mask in enumerate(crc_rules["frame_crc_masks"]):
            values[f"FRAME_CRC_MASK{index}"] = mask
        values["FRAME_PARITY_MASK"] = crc_rules["frame_parity_mask"]
    return values


def validate_protocol_tables(data):
    """检查已核验规则的完整性和代际表维度，空表仍由关口报告阻塞。"""
    tables = data["protocol_values"]
    for name, value in tables.items():
        if value is None:
            continue
        if not isinstance(value, dict) or not value.get("source"):
            raise ValueError(f"协议规则缺少原文定位 {name}")
    timers = tables["ack_replay_limits"]
    if timers:
        for generation in range(1, 5):
            rows = timers["ack_symbols"][str(generation)]
            if set(rows) != {"128", "256"}:
                raise ValueError("ACK 表缺少首版 MPS 档位")
            for row in rows.values():
                if len(row) != 3 or any(
                    type(x) is not int or x <= 0 for x in row
                ):
                    raise ValueError("ACK 表缺少首版链路宽度")
        for mode in ("normal", "extended"):
            rules = timers["simplified_replay_symbols"]
            low, high = rules[mode + "_range"]
            if not low <= rules["selected_" + mode] <= high:
                raise ValueError("replay 选择超出协议允许范围")
    framing = tables["framing_rules"]
    if framing:
        for generation in range(1, 5):
            entry = framing["skp_schedule"][str(generation)]
            low, high = entry["range"]
            if not low <= entry["selected"] <= high:
                raise ValueError("SKP 周期超出协议允许范围")
    fc = tables["fc_state_machine"]
    if fc and not fc["feature_exchange_required_for_gen4_support"]:
        raise ValueError("Gen4 端口需要支持 Scaled Flow Control")


def verify_artifact(data, identifier, path):
    """复算本地原件的字节数与摘要，避免镜像更新后沿用旧核验结果。"""
    source = next((s for s in data["sources"] if s["id"] == identifier), None)
    if source is None or "artifact_sha256" not in source:
        raise ValueError(f"来源未登记原件摘要 {identifier}")
    raw = Path(path).read_bytes()
    if hashlib.sha256(raw).hexdigest() != source["artifact_sha256"] or len(
        raw
    ) != source.get("artifact_bytes", len(raw)):
        raise ValueError(f"来源原件与登记摘要或字节数不一致 {identifier}")


def gate_status(data):
    """按依据和实施状态推导关口，返回可供 CI 使用的报告。"""
    validate(data)
    missing = [
        {"id": item["id"], "title": item["title"], "missing": item["missing"]}
        for item in data["coverage"]
        if item["status"] in {"partial", "blocked"}
    ]
    missing_tables = [
        name for name, value in data["protocol_values"].items() if not value
    ]
    result = {
        "blocking_facts": missing,
        "missing_tables": missing_tables,
        "milestones": {},
    }
    previous_complete = True
    for milestone in data["milestones"]:
        name = milestone["id"]
        if name == "G0" and (missing or missing_tables):
            state = "source_blocked"
        elif not previous_complete:
            state = "waiting_for_" + milestone["depends_on"][0]
        else:
            state = milestone["implementation"]
        result["milestones"][name] = state
        previous_complete = state == "complete"
    return result


def outputs(data, raw):
    """生成共享常量和可审计覆盖矩阵，嵌入档案摘要。"""
    values = validate(data)
    report = gate_status(data)
    digest = hashlib.sha256(raw).hexdigest()
    stamp = f"gw100-link.json SHA-256={digest}"
    c = [
        "/**",
        " * @file  gw100_pcie_defs.h",
        " * @brief PCIe 共享定义，请勿手改",
        f" * {stamp}",
        " * @see spec-pcie",
        " */",
        "#ifndef GW100_PCIE_DEFS_H",
        "#define GW100_PCIE_DEFS_H",
        "#include <stdint.h>",
        "",
    ]
    cc = [
        "/**",
        " * @file  link_defs.hh",
        " * @brief PCIe 共享定义，请勿手改",
        f" * {stamp}",
        " * @see spec-pcie",
        " */",
        "#ifndef GW100_PCIE_LINK_DEFS_HH",
        "#define GW100_PCIE_LINK_DEFS_HH",
        "#include <cstdint>",
        "",
        "namespace gem5::gw100::pciedefs {",
        "",
    ]
    py = [
        '"""',
        "@file  pcie_defs.py",
        "@brief PCIe 共享定义，请勿手改",
        stamp,
        "@see spec-pcie",
        '"""',
        "",
    ]
    for name, value in values.items():
        c.append(f"#define GW_PCIE_{name} UINT64_C({value})")
        cc.append(f"inline constexpr uint64_t {name} = {value}ULL;")
        py.append(f"{name} = {value}")
    c.extend(["", "#endif", ""])
    cc.extend(["", "} // namespace gem5::gw100::pciedefs", "#endif", ""])
    py.append("")
    doc = [
        "# PCIe 链路来源与覆盖矩阵",
        "",
        "<!-- 由 tools/pcie_spec.py 生成，请勿手改 -->",
        "",
        f"档案版本为 {data['model_version']}。{stamp}",
        "",
        "## 实施关口",
        "",
        "| 关口 | 当前状态 |",
        "|---|---|",
    ]
    for name, state in report["milestones"].items():
        doc.append(f"| {name} | {state} |")
    doc.extend(
        [
            "",
            "当前表区分来源核验、参考设计和实现验收。",
            "共享定义覆盖已核验常量和已批准预算，后续链路实现依关口推进。",
            "",
            "## 来源版本",
            "",
        ]
    )
    for source in data["sources"]:
        doc.extend(
            [
                f"### {source['id']}",
                "",
                f"{source['title']}，{source['version']}。",
                f"发布标记为 {source['published'] or '在线 FAQ 或目录'}，"
                f"核验日期为 {source['reviewed_on']}。",
                "",
                f"定位为 {source['locator']}。{source['review']} "
                f"[原始来源]({source['url']})",
                "",
            ]
        )
        if "artifact_sha256" in source:
            doc.extend(
                [
                    f"原件 SHA-256 为 `{source['artifact_sha256']}`。",
                    "",
                ]
            )
        if source.get("kind") == "original_spec_mirror":
            doc.extend(
                [
                    f"原版镜像由 {source['publisher']} 编写，"
                    f"固定提交为 `{source.get('mirror_commit', '不适用')}`。",
                    f"{source['revision_check']} "
                    f"[官方书目]({source['official_catalog_url']})",
                    "",
                ]
            )
    doc.extend(
        [
            "## 协议规则档案",
            "",
            "结构化规则与原文定位保存在同一共享档案的 "
            "`protocol_values` 中。",
            "",
        ]
    )
    for name, value in data["protocol_values"].items():
        doc.append(f"- `{name}`。{value['source'] if value else '待核验'}")
    doc.append("")
    doc.extend(
        [
            "## 共享预算",
            "",
            "| 常量 | 数值 | 单位 | 依据 |",
            "|---|---|---|---|",
        ]
    )
    for item in data["definitions"]:
        doc.append(
            f"| {item['name']} | {item['value']} | {item['unit']} | "
            f"{item['basis']} |"
        )
    doc.extend(
        [
            "",
            "## 行为覆盖",
            "",
            "实现和测试列定位相应源码与检查，验收结果见 verification.md。",
            "",
        ]
    )
    for item in data["coverage"]:
        doc.extend(
            [
                f"### {item['id']} {item['title']}",
                "",
                f"依据状态为 {item['status']}，核验范围为 "
                f"{', '.join(item['verified_scope']) or '尚未覆盖完整代际'}。",
                "",
                f"来源为 {', '.join(item['evidence']) or '用户批准的参考设计'}。",
                "",
                f"实现落点为 {item['planned_implementation']}。",
                "",
                f"验证落点为 {', '.join(item['planned_tests'])}。",
                "",
            ]
        )
        if item["missing"]:
            doc.extend(["待核验内容如下。", ""])
            doc.extend(f"- {entry}" for entry in item["missing"])
            doc.append("")
    return {
        "firmware/common/generated/gw100_pcie_defs.h": "\n".join(c),
        "gem5-ext/src/gw100/pcie/link/generated/link_defs.hh": "\n".join(cc),
        "gem5-ext/configs/gw100/generated/pcie_defs.py": "\n".join(py),
        "docs/spec/host/spec-pcie/link-coverage.md": "\n".join(doc),
    }


def main():
    """生成定义或执行检查，资料和实施关口未完成时返回明确状态。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--status", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--gate", choices=("G0", "G1", "G2", "G3"))
    parser.add_argument(
        "--verify-artifact", action="append", default=[], metavar="SOURCE=PATH"
    )
    args = parser.parse_args()
    try:
        raw = PROFILE.read_bytes()
        data = json.loads(raw)
        generated = outputs(data, raw)
        for item in args.verify_artifact:
            identifier, path = item.split("=", 1)
            verify_artifact(data, identifier, path)
    except (KeyError, TypeError, ValueError, OSError) as error:
        parser.exit(1, f"PCIe 档案无效 {error}\n")
    if args.check or args.gate:
        for name, content in generated.items():
            path = ROOT / name
            if not path.exists() or path.read_text() != content:
                parser.exit(1, f"PCIe 共享定义需要重新生成 {name}\n")
    elif not (args.status or args.gate or args.json or args.verify_artifact):
        for name, content in generated.items():
            path = ROOT / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    report = gate_status(data)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print("PCIe 档案结构检查通过")
        if args.check or args.gate:
            print("PCIe 生成文件一致")
        for name, state in report["milestones"].items():
            print(f"{name} {state}")
        for item in report["blocking_facts"]:
            print(f"资料关口 {item['id']} {item['title']}")
    if args.gate and report["milestones"][args.gate] != "complete":
        raise SystemExit(2)


if __name__ == "__main__":
    main()
