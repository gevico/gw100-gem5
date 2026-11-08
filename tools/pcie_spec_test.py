"""
@file  pcie_spec_test.py
@brief 验证 PCIe 共享档案、资源边界与资料关口

覆盖未对齐完成预留、信用单位、来源归属和后续关口的依赖。

@see spec-pcie
"""

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import pcie_spec


class PcieSpecTest(unittest.TestCase):
    """共享定义与关口报告的离线回归。"""

    def setUp(self):
        """每个测试使用独立档案副本。"""
        self.raw = pcie_spec.PROFILE.read_bytes()
        self.data = json.loads(self.raw)

    def definition(self, name):
        """返回指定共享常量的档案条目。"""
        return next(
            item for item in self.data["definitions"] if item["name"] == name
        )

    def test_baseline_resources(self):
        """基线预算与十二种链路组合一致。"""
        values = pcie_spec.validate(self.data)
        self.assertEqual(values["COMPLETION_DATA_BYTES"], 32 * 1024)
        self.assertEqual(values["COMPLETION_HEADER_SLOTS"], 576)
        scope = self.data["scope"]
        self.assertEqual(len(scope["generations"]) * len(scope["widths"]), 12)

    def test_unaligned_header_reservation(self):
        """512 B 非对齐读可能覆盖九个最小 RCB 区间。"""
        self.definition("COMPLETION_HEADER_SLOTS")["value"] = 512
        with self.assertRaisesRegex(ValueError, "完成头预留不足"):
            pcie_spec.validate(self.data)

    def test_rc_posted_reservation(self):
        """分类预留覆盖 posted 进展，同时保留 NP 服务容量。"""
        for value in (0, 64, 65):
            with self.subTest(value=value):
                self.definition("RC_POSTED_RESERVED_SLOTS")["value"] = value
                with self.assertRaisesRegex(ValueError, "posted 预留"):
                    pcie_spec.validate(self.data)

    def test_completion_data_reservation(self):
        """请求总额受到实际完成数据容量约束。"""
        self.definition("COMPLETION_DATA_BYTES")["value"] -= 1
        with self.assertRaisesRegex(ValueError, "完成数据预留不足"):
            pcie_spec.validate(self.data)

    def test_data_credit_alignment(self):
        """信用预算以完整协议单位表示。"""
        for name in (
            "RX_PD_BYTES",
            "RX_NPD_BYTES",
            "TX_PD_BYTES",
            "TX_NPD_BYTES",
            "TX_CPLD_BYTES",
        ):
            with self.subTest(name=name):
                data = deepcopy(self.data)
                item = next(
                    item
                    for item in data["definitions"]
                    if item["name"] == name
                )
                item["value"] += 1
                with self.assertRaisesRegex(ValueError, "完整信用单位"):
                    pcie_spec.validate(data)

    def test_configuration_write_budget(self):
        """配置写需要 non-posted 数据资源。"""
        self.definition("RX_NPD_BYTES")["value"] = 16
        with self.assertRaisesRegex(ValueError, "配置写"):
            pcie_spec.validate(self.data)

    def test_replay_alignment(self):
        """replay 存储粒度可用于确定性分配。"""
        self.definition("REPLAY_ALIGNMENT_BYTES")["value"] = 24
        with self.assertRaisesRegex(ValueError, "二次幂"):
            pcie_spec.validate(self.data)

    def test_duplicate_or_noninteger_constant(self):
        """重复名称、浮点数和布尔值均被拒绝。"""
        self.data["definitions"].append(deepcopy(self.data["definitions"][0]))
        with self.assertRaisesRegex(ValueError, "重复或非法常量"):
            pcie_spec.validate(self.data)
        self.data["definitions"].pop()
        for value in (-1, 1.5, True, 2**64):
            with self.subTest(value=value):
                self.definition("RX_PH")["value"] = value
                with self.assertRaisesRegex(ValueError, "uint64"):
                    pcie_spec.validate(self.data)

    def test_constant_cannot_depend_on_missing_fact(self):
        """未核验协议参数无法进入共享数值。"""
        item = next(
            item for item in self.data["coverage"] if item["id"] == "TIMERS"
        )
        item.update(status="blocked", verified_scope=[], missing=["测试缺口"])
        self.definition("ACK_BATCH_TLPS")["fact"] = "TIMERS"
        with self.assertRaisesRegex(ValueError, "未收敛的依据"):
            pcie_spec.validate(self.data)

    def test_required_coverage_is_complete(self):
        """删除阻塞项会导致结构检查失败。"""
        self.data["coverage"] = [
            item for item in self.data["coverage"] if item["id"] != "TIMERS"
        ]
        with self.assertRaisesRegex(ValueError, "必要协议覆盖项"):
            pcie_spec.validate(self.data)

    def test_protocol_fact_cannot_be_design_override(self):
        """协议定时缺口受到独立的依据类别检查。"""
        item = next(
            item for item in self.data["coverage"] if item["id"] == "TIMERS"
        )
        item["status"] = "design"
        item["missing"] = []
        item["verified_scope"] = item["required_scope"]
        with self.assertRaisesRegex(ValueError, "协议事实"):
            pcie_spec.validate(self.data)

    def test_partial_evidence_cannot_be_verified(self):
        """核验状态必须覆盖全部规定代际。"""
        item = next(
            item for item in self.data["coverage"] if item["id"] == "FRAMING"
        )
        item["status"] = "verified"
        item["missing"] = []
        item["verified_scope"] = ["Gen3"]
        with self.assertRaisesRegex(ValueError, "未核验范围冲突"):
            pcie_spec.validate(self.data)

    def test_official_source_and_fact_reference(self):
        """来源目录和不明转载无法充当具体协议依据。"""
        self.data["sources"][0]["url"] = "https://example.com/spec.pdf"
        with self.assertRaisesRegex(ValueError, "公开官方来源"):
            pcie_spec.validate(self.data)
        self.data = json.loads(self.raw)
        self.data["coverage"][0]["evidence"] = ["SIG-CATALOG"]
        with self.assertRaisesRegex(ValueError, "可用协议依据"):
            pcie_spec.validate(self.data)

    def test_source_blocker_propagates(self):
        """后续实施状态遵循来源关口。"""
        lifecycle = next(
            item
            for item in self.data["coverage"]
            if item["id"] == "ERROR_RESET"
        )
        lifecycle["status"] = "blocked"
        lifecycle["verified_scope"] = []
        lifecycle["missing"] = ["测试中的明确资料缺口"]
        for milestone in self.data["milestones"]:
            milestone["implementation"] = "complete"
        report = pcie_spec.gate_status(self.data)
        self.assertEqual(
            report["milestones"],
            {
                "G0": "source_blocked",
                "G1": "waiting_for_G0",
                "G2": "waiting_for_G1",
                "G3": "waiting_for_G2",
            },
        )
        self.assertIn(
            "ERROR_RESET", {item["id"] for item in report["blocking_facts"]}
        )

    def test_source_fingerprint(self):
        """已保存原件的摘要使用完整 SHA-256 编码。"""
        self.data["sources"][0]["artifact_sha256"] = "unverified"
        with self.assertRaisesRegex(ValueError, "非法原件摘要"):
            pcie_spec.validate(self.data)

    def test_original_mirror_requires_provenance(self):
        """允许原版镜像仍须保存原件和版本核验信息。"""
        source = self.data["sources"][0]
        source.update(
            kind="original_spec_mirror", url="https://example.com/base.pdf"
        )
        with self.assertRaisesRegex(ValueError, "缺少溯源信息"):
            pcie_spec.validate(self.data)

    def test_original_github_mirror_is_pinned(self):
        """移动分支不能替代已核验的原件固定提交。"""
        source = deepcopy(self.data["sources"][0])
        commit = "1" * 40
        source.update(
            kind="original_spec_mirror",
            publisher="PCI-SIG",
            artifact_sha256="2" * 64,
            artifact_bytes=12345,
            document_date="2017-09-27",
            revision_check="核验封面和修订记录",
            mirror_authorization="user-2026-11-07",
            mirror_commit=commit,
            official_catalog_url="https://pcisig.com/PCIExpress/Specs/Base/4",
            url=f"https://raw.githubusercontent.com/a/b/{commit}/base.pdf",
        )
        pcie_spec.validate_source(source)
        source["url"] = source["url"].replace(commit, "master")
        with self.assertRaisesRegex(ValueError, "固定提交"):
            pcie_spec.validate_source(source)

    def test_ack_dimensions_and_positive_values(self):
        """协议表维度不能静默遗漏某种已声明链路组合。"""
        table = self.data["protocol_values"]["ack_replay_limits"]
        table["ack_symbols"]["4"]["256"].pop()
        with self.assertRaisesRegex(ValueError, "链路宽度"):
            pcie_spec.validate(self.data)

    def test_replay_selection_within_verified_limits(self):
        """参考实现的 replay 期限受原文范围约束。"""
        table = self.data["protocol_values"]["ack_replay_limits"]
        table["simplified_replay_symbols"]["selected_normal"] = 23999
        with self.assertRaisesRegex(ValueError, "replay 选择"):
            pcie_spec.validate(self.data)

    def test_skp_selection_within_verified_limits(self):
        """Gen4 SKP 调度周期使用规范允许的范围。"""
        table = self.data["protocol_values"]["framing_rules"]
        table["skp_schedule"]["4"]["selected"] = 376
        with self.assertRaisesRegex(ValueError, "SKP 周期"):
            pcie_spec.validate(self.data)

    def test_local_artifact_hash_and_length(self):
        """原件校验同时检查内容摘要和登记字节数。"""
        source = self.data["sources"][0]
        raw = b"%PDF-test-fixture"
        source.update(
            artifact_sha256=hashlib.sha256(raw).hexdigest(),
            artifact_bytes=len(raw),
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.pdf"
            path.write_bytes(raw)
            pcie_spec.verify_artifact(self.data, source["id"], path)
            source["artifact_bytes"] += 1
            with self.assertRaisesRegex(ValueError, "不一致"):
                pcie_spec.verify_artifact(self.data, source["id"], path)
            source["artifact_bytes"] -= 1
            path.write_bytes(raw + b"changed")
            with self.assertRaisesRegex(ValueError, "不一致"):
                pcie_spec.verify_artifact(self.data, source["id"], path)

    def test_missing_rule_tables_block_gate(self):
        """空规则表可以独立阻止关口被提前关闭。"""
        for item in self.data["coverage"]:
            if item["status"] != "design":
                item["status"] = "verified"
                item["missing"] = []
                item["verified_scope"] = item["required_scope"]
        self.data["protocol_values"] = {
            name: None for name in pcie_spec.PROTOCOL_TABLES
        }
        self.data["milestones"][0]["implementation"] = "complete"
        report = pcie_spec.gate_status(self.data)
        self.assertEqual(report["milestones"]["G0"], "source_blocked")
        self.assertEqual(
            set(report["missing_tables"]), pcie_spec.PROTOCOL_TABLES
        )

    def test_generation_is_deterministic(self):
        """四端生成物带有同一档案摘要。"""
        expected = pcie_spec.outputs(self.data, self.raw)
        self.assertEqual(expected, pcie_spec.outputs(self.data, self.raw))
        digest = hashlib.sha256(self.raw).hexdigest()
        self.assertEqual(len(expected), 4)
        for name, content in expected.items():
            with self.subTest(name=name):
                self.assertIn(digest, content)
                self.assertEqual((pcie_spec.ROOT / name).read_text(), content)

    def test_cli_reports_gate_without_writing(self):
        """状态查询、生成检查和关口检查使用独立的退出语义。"""
        script = str(Path(pcie_spec.__file__))
        before = pcie_spec.PROFILE.read_bytes()
        expected = pcie_spec.gate_status(self.data)
        result = subprocess.run(
            [sys.executable, script, "--status", "--json"],
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(json.loads(result.stdout), expected)
        result = subprocess.run(
            [sys.executable, script, "--check", "--gate", "G0"],
            capture_output=True,
            text=True,
        )
        complete = expected["milestones"]["G0"] == "complete"
        self.assertEqual(result.returncode, 0 if complete else 2)
        self.assertIn(expected["milestones"]["G0"], result.stdout)
        self.assertEqual(before, pcie_spec.PROFILE.read_bytes())

    def test_parameter_change_updates_outputs(self):
        """参数和摘要一起传播到全部生成物。"""
        self.definition("ONE_WAY_PROPAGATION_PS")["value"] = 5001
        raw = (json.dumps(self.data, indent=2) + "\n").encode()
        generated = pcie_spec.outputs(self.data, raw)
        digest = hashlib.sha256(raw).hexdigest()
        for name, content in generated.items():
            with self.subTest(name=name):
                self.assertIn(digest, content)
                self.assertIn("5001", content)


if __name__ == "__main__":
    unittest.main()
