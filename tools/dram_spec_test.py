"""
@file  dram_spec_test.py
@brief 验证 DRAM 来源检查的失败门控及未知值不会进入共享定义

这是 G0 工具的回归，不计入器件、固件或性能覆盖率。
运行：.venv/bin/python -m unittest discover -s tools -p 'dram_spec_test.py' -v

@see spec-dram
"""

import contextlib
import copy
import hashlib
import io
import json
import tempfile
import unittest
from pathlib import Path

import dram_spec


class SourceGateTest(unittest.TestCase):
    """检查错误证据、未知能力和不完整关口不会被静默接受。"""

    def setUp(self):
        """为每个用例载入独立档案，避免修改工作区真源。"""
        self.profile, self.content = dram_spec.load_profile(
            dram_spec.ROOT / dram_spec.PROFILE)

    def test_known_source_and_generated_files(self):
        """核对真实原件与已提交生成物，不能仅验证模板能自洽。"""
        dram_spec.verify_manual(self.profile, dram_spec.ROOT)
        for path, expected in dram_spec.generated_files(
                self.profile, self.content).items():
            self.assertEqual((dram_spec.ROOT / path).read_text(), expected, path)

    def test_unknown_capabilities_are_not_zero(self):
        """未知只读字段不能生成一个看似有效的零值。"""
        names = {name for name, _, _ in dram_spec.definitions(self.profile)}
        for item in self.profile["capabilities"]:
            if item["value"] is None:
                self.assertNotIn("CAP_" + item["id"].upper(), names)
        self.assertIn("CAP_LINK_ECC", names)

    def test_review_switch_cannot_hide_missing_evidence(self):
        """把审查总状态改为通过，仍不得跳过未解决的资料。"""
        self.profile["review"] = {
            "status": "passed", "field_coverage_complete": True,
            "full_timing_table_complete": True, "interface_complete": True
        }
        pending = dram_spec.blockers(self.profile)
        self.assertTrue(any("rfm_parameters" in item for item in pending))
        self.assertTrue(any("eight_bank" in item for item in pending))
        for path, content in dram_spec.generated_files(
                self.profile, json.dumps(self.profile).encode()).items():
            if path.suffix == ".h":
                self.assertIn("#define GW100_DRAM_SPEC_G0_READY 0", content)

    def test_strict_cli_exit_code(self):
        """严格验收必须返回资料阻塞，不能以生成成功报告 G0 通过。"""
        with contextlib.redirect_stdout(io.StringIO()) as output:
            result = dram_spec.main(["--check", "--require-g0"])
        self.assertEqual(result, 2)
        self.assertIn("G0: BLOCKED", output.getvalue())

    def test_unknown_cannot_be_marked_verified(self):
        """未知能力标为已核对属于错误档案。"""
        item = next(i for i in self.profile["capabilities"] if i["value"] is None)
        item["verified"] = True
        with self.assertRaises(dram_spec.SpecError):
            dram_spec.validate_profile(self.profile)

    def test_capacity_mismatch(self):
        """封装与 rank 容量不一致时，拒绝生成错误的 32 GiB 拓扑。"""
        self.profile["constants"][0]["value"] = 3
        with self.assertRaisesRegex(dram_spec.SpecError, "容量"):
            dram_spec.validate_profile(self.profile)

    def test_bool_is_not_numeric_parameter(self):
        """JSON 布尔值不能通过整数参数检查。"""
        self.profile["constants"][0]["value"] = True
        with self.assertRaises(dram_spec.SpecError):
            dram_spec.validate_profile(self.profile)

    def test_lpddr5_table_rejected(self):
        """避免把相邻的 LPDDR5 核心时序表误用为 LPDDR5X。"""
        self.profile["timings"][0]["pages"] = [527]
        with self.assertRaisesRegex(dram_spec.SpecError, "LPDDR5 表"):
            dram_spec.validate_profile(self.profile)

    def test_time_anchor_required(self):
        """缺少 ACT-2 等锚点的数字不能作为完整时序定义。"""
        del self.profile["timings"][0]["anchor"]
        with self.assertRaises(dram_spec.SpecError):
            dram_spec.validate_profile(self.profile)

    def test_default_outside_mask_rejected(self):
        """已知位之外的数据不得被包装成复位值。"""
        self.profile["mr_defaults"][0]["value"] = 1
        with self.assertRaisesRegex(dram_spec.SpecError, "掩码"):
            dram_spec.validate_profile(self.profile)

    def test_duplicate_default_rejected(self):
        """同一 MR 的定义不能生成重名宏或重定义 C++ 常量。"""
        self.profile["mr_defaults"].append(
            copy.deepcopy(self.profile["mr_defaults"][0]))
        with self.assertRaises(dram_spec.SpecError):
            dram_spec.validate_profile(self.profile)

    def test_duplicate_json_key_rejected(self):
        """不得用 JSON 后出现的键覆盖审查过的参数。"""
        with self.assertRaises(dram_spec.SpecError):
            json.loads('{"value": 1, "value": 2}',
                       object_pairs_hook=dram_spec.reject_duplicate_keys)

    def test_unclassified_feature_rejected(self):
        """每项来源覆盖必须明确处于四类边界中的一类。"""
        del self.profile["coverage"][0]["classification"]
        with self.assertRaises(dram_spec.SpecError):
            dram_spec.validate_profile(self.profile)

    def test_page_outside_manual_rejected(self):
        """引用不存在的页面必须失败。"""
        self.profile["timings"][0]["pages"] = [620]
        with self.assertRaises(dram_spec.SpecError):
            dram_spec.validate_profile(self.profile)

    def test_changed_manual_rejected(self):
        """大小相同、摘要不同的原件也必须失败。"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "reference/example.pdf"
            path.parent.mkdir()
            original = b"original"
            path.write_bytes(b"modified")
            self.profile["manual"].update({
                "path": "reference/example.pdf", "bytes": len(original),
                "sha256": hashlib.sha256(original).hexdigest()
            })
            with self.assertRaisesRegex(dram_spec.SpecError, "SHA-256"):
                dram_spec.verify_manual(self.profile, root)

    def test_mmio_overlap_rejected(self):
        """新增寄存器不能覆盖相邻字段或保护表。"""
        reg = copy.deepcopy(self.profile["mmio"]["common"][0])
        reg["name"] = "OVERLAP"
        self.profile["mmio"]["common"].append(reg)
        with self.assertRaisesRegex(dram_spec.SpecError, "重叠"):
            dram_spec.validate_profile(self.profile)

    def test_mmio_alignment_rejected(self):
        """64 位寄存器需要按其访问宽度对齐。"""
        reg = next(i for i in self.profile["mmio"]["channel"] if i["width"] == 64)
        reg["offset"] += 4
        with self.assertRaisesRegex(dram_spec.SpecError, "对齐"):
            dram_spec.validate_profile(self.profile)


if __name__ == "__main__":
    unittest.main()
