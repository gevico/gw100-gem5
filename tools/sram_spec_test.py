"""
@file  sram_spec_test.py
@brief SRAM 档案、生成物和预算检查
@see spec-sram
"""

import copy
import json
import unittest
import sram_spec


class SramSpecTest(unittest.TestCase):
    def setUp(self):
        self.raw = sram_spec.PROFILE.read_bytes()
        self.data = json.loads(self.raw)

    def test_generated(self):
        for path, expected in sram_spec.outputs(self.data, self.raw).items():
            self.assertEqual(path.read_text(), expected)

    def test_missing_parameter(self):
        for name, profile in self.data["profiles"].items():
            for field in profile:
                broken = copy.deepcopy(self.data)
                del broken["profiles"][name][field]
                with self.assertRaises(ValueError):
                    sram_spec.validate(broken)

    def test_illegal_value(self):
        for field, value in [
            ("banks", 3),
            ("word_bytes", 4),
            ("mask_bytes", 32),
            ("size", 1023),
            ("slots", 0),
            ("read_cycles", True),
        ]:
            broken = copy.deepcopy(self.data)
            broken["profiles"]["management"][field] = value
            with self.assertRaises(ValueError):
                sram_spec.validate(broken)

    def test_wait_bound(self):
        broken = copy.deepcopy(self.data)
        broken["profiles"]["management"]["read_cycles"] = 8
        with self.assertRaises(ValueError):
            sram_spec.validate(broken)
        broken = copy.deepcopy(self.data)
        broken["profiles"]["management"]["interval_cycles"] = 8
        with self.assertRaises(ValueError):
            sram_spec.validate(broken)

    def test_budgets(self):
        m = self.data["profiles"]["management"]
        self.assertEqual(
            m["banks"] * m["word_bytes"] * 10**12 / m["period_ps"],
            32_000_000_000,
        )
        c = self.data["profiles"]["compute_1rw"]
        self.assertEqual(
            c["banks"] * c["word_bytes"] * 10**12 / c["period_ps"],
            256_000_000_000,
        )
        self.assertEqual(m["size"] // 8, 262144)
        self.assertEqual(
            m["size"] // m["banks"] // m["word_bytes"] * m["boot_period_ps"],
            1_310_720_000,
        )

    def test_firmware_layout(self):
        root = sram_spec.ROOT
        firmware = (root / "firmware/fw/fw.ld").read_text()
        rom = (root / "firmware/rom/rom.ld").read_text()
        crt = (root / "firmware/rom/crt0.S").read_text()
        sram_spec.validate_layout(2 << 20, firmware, rom, crt)
        for fw, boot, stack in (
            (firmware.replace("1M - 4K", "1M"), rom, crt),
            (firmware, rom.replace("0x101FF000", "0x101FE000"), crt),
            (firmware, rom, crt.replace("0x10200000", "0x10400000")),
        ):
            with self.assertRaises(ValueError):
                sram_spec.validate_layout(2 << 20, fw, boot, stack)


if __name__ == "__main__":
    unittest.main()
