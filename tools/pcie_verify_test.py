"""
@file  pcie_verify_test.py
@brief 独立 PCIe 校验程序的固定向量与约束回归
"""

import unittest

from pcie_verify import (
    data_end,
    latency_reference,
    parse_tlp,
    require,
    throughput_reference,
    wire_crc,
)


class ReferenceTest(unittest.TestCase):
    """参考自身使用独立固定期望，防止调整期望值迁就被测模型。"""

    def test_crc32_published_mapping(self):
        """固定字节向量同时约束多项式、输入位序和字段字节序。"""
        self.assertEqual(
            wire_crc(b"123456789", 32, 0x04C11DB7).hex(), "2639f4cb"
        )

    def test_crc16_published_mapping(self):
        """DLLP 四零字节的固定校验字段。"""
        self.assertEqual(wire_crc(bytes(4), 16, 0x100B).hex(), "b362")

    def test_crc_detects_single_bit_errors(self):
        """输入每一位的翻转改变 CRC。"""
        original = bytes.fromhex("00000000010000ff00001000")
        expected = wire_crc(original, 32, 0x04C11DB7)
        for bit in range(len(original) * 8):
            damaged = bytearray(original)
            damaged[bit // 8] ^= 1 << (bit % 8)
            self.assertNotEqual(wire_crc(damaged, 32, 0x04C11DB7), expected)

    def test_serialization_twelve_modes(self):
        """整块和部分块直接按位周期核对。"""
        for generation, half_bit in enumerate((800, 400, 250, 125), 1):
            for width in (1, 2, 4):
                symbols = 280 // width
                if generation < 3:
                    expected = symbols * 10 * half_bit
                else:
                    blocks, rest = divmod(symbols, 16)
                    expected = blocks * 130 + (2 + rest * 8 if rest else 0)
                    expected *= half_bit
                self.assertEqual(
                    data_end(10000, symbols, half_bit, generation),
                    expected + 10000,
                )

    def test_memory_request_fields(self):
        """原始头与独立整数期望对应。"""
        raw = bytes.fromhex("400000010100050f00001000aabbccdd")
        parsed = parse_tlp(raw)
        self.assertEqual(parsed["requester"], 0x100)
        self.assertEqual(parsed["tag"], 5)
        self.assertEqual(parsed["traffic"], 0)
        self.assertEqual(parsed["address"], 0x1000)
        self.assertEqual(parsed["size"], 4)
        self.assertEqual(parsed["payload"], bytes.fromhex("aabbccdd"))

    def test_rejects_cross_page_request(self):
        """越过 4 KiB 的请求不进入合法性能期望。"""
        with self.assertRaisesRegex(ValueError, "4 KiB"):
            parse_tlp(bytes.fromhex("00000002010005ff00001ffc"))

    def test_explicit_assertion(self):
        """检查失败保留原始原因。"""
        with self.assertRaisesRegex(ValueError, "resource"):
            require(False, "resource")

    def test_reference_uses_backend_resource_limit(self):
        """有限后端服务可限制 posted 吞吐。"""
        result = dict(
            generation=4,
            width=4,
            mps=256,
            mrrs=512,
            completion=256,
            operation="write",
            duplex=0,
            service_ps=10000000,
        )
        self.assertEqual(throughput_reference(result), 64 * 256 / 10000000)

    def test_dma_duplex_wire_conservation(self):
        """512 B MRd、128 B 完成及写包同时满足双向线速方程。"""
        result = dict(
            generation=4,
            width=4,
            mps=128,
            mrrs=512,
            completion=128,
            operation="dma",
            duplex=1,
            service_ps=2500000,
            write_service_ps=250000,
        )
        reads, writes = throughput_reference(result)
        raw = 64 / 8125 * 369 / 370
        self.assertAlmostEqual(reads * 601 / 512 + writes * 9 / 128, raw)
        self.assertAlmostEqual(reads * 3 / 64 + writes * 37 / 32, raw)
        self.assertAlmostEqual(reads * 1e3, 6.300655539355444)
        self.assertAlmostEqual(writes * 1e3, 6.538629939223186)

    def test_duplex_read_backend_partition(self):
        """EP 总槽数 64，RC 的 NP 分类上限为 64−16。"""
        result = dict(
            generation=4,
            width=4,
            mps=256,
            mrrs=128,
            completion=256,
            operation="read",
            duplex=1,
            service_ps=10000000,
        )
        self.assertEqual(
            throughput_reference(result),
            [64 * 128 / 10000000, 48 * 128 / 10000000],
        )

    def test_isolated_latency_fixed_edges(self):
        """固定起点按 8b/10b 与 128b/130b 手算两级流水。"""
        for generation, width, visible in ((1, 1, 360000), (4, 4, 32000)):
            result = dict(
                generation=generation,
                width=width,
                service_ps=0,
                latency_probe=dict(
                    start_ps=0, visible_ps=visible, origin_half=0, symbols=0
                ),
            )
            self.assertEqual(latency_reference(result)["expected_ps"], visible)
            result["latency_probe"]["visible_ps"] += 2
            with self.assertRaisesRegex(ValueError, "逐边沿"):
                latency_reference(result)


if __name__ == "__main__":
    unittest.main()
