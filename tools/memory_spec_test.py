"""
@file  memory_spec_test.py
@brief 通用内存档案的容量、编码与生成检查

@see spec-dram
"""

import copy
import json
import unittest

import memory_spec


class MemorySpecTest(unittest.TestCase):
    """拒绝会造成模型和固件不一致的档案。"""

    def setUp(self):
        """读取生产档案，测试只修改内存副本。"""
        self.raw = memory_spec.PROFILE.read_bytes()
        self.data = json.loads(self.raw)

    def change(self, name, value):
        """替换内存副本中的指定常量。"""
        for items in self.data["definitions"].values():
            for item in items:
                if item["name"] == name:
                    item["value"] = value
                    return
        self.fail(f"找不到常量 {name}")

    def test_capacity(self):
        """物理组织必须恰好提供全部容量。"""
        self.change("DEVICE_BYTES", 1 << 30)
        with self.assertRaises(ValueError):
            memory_spec.definitions(self.data)

    def test_duplicate_encoding(self):
        """同类寄存器和枚举编码不得重复。"""
        self.change("CMD_TRAIN", 1)
        with self.assertRaises(ValueError):
            memory_spec.definitions(self.data)

    def test_invalid_values(self):
        """拒绝 bool、负数和非二次幂交织。"""
        for value in (True, -1, 192):
            with self.subTest(value=value):
                self.setUp()
                self.change("INTERLEAVE_BYTES", value)
                with self.assertRaises(ValueError):
                    memory_spec.definitions(self.data)

    def test_quantization(self):
        """时钟必须与目标数据率的量化一致。"""
        self.change("CK_PS", 950)
        with self.assertRaises(ValueError):
            memory_spec.definitions(self.data)

    def test_reproducible_outputs(self):
        """三端生成物与磁盘一致且生成过程不修改输入。"""
        before = copy.deepcopy(self.data)
        for name, content in memory_spec.outputs(self.data, self.raw).items():
            self.assertEqual((memory_spec.ROOT / name).read_text(), content)
        self.assertEqual(self.data, before)


if __name__ == "__main__":
    unittest.main()
