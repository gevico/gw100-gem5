"""
@file  noc_spec_test.py
@brief NoC G0 的映射、报文、资源、相位和契约检查

固定向量与逐周期手算独立于生成器公式。运行期协议与吞吐实测由
G1 至 G3 的夹具验收。

@see spec-noc
"""

import copy
import importlib.util
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import noc_spec as noc


class NocSpecTest(unittest.TestCase):
    """生产档案只读，所有负例修改内存副本。"""

    @classmethod
    def setUpClass(cls):
        """复用已核验的内存和 PCIe 依赖。"""
        cls.raw = noc.PROFILE.read_bytes()
        cls.profile = json.loads(cls.raw)
        cls.dependencies = noc.load_dependencies(cls.profile)

    def setUp(self):
        """为每个用例建立独立档案。"""
        self.data = copy.deepcopy(self.profile)
        self.v = noc.validate(self.data, self.dependencies)
        self.memory = self.dependencies["memory"]

    def change(self, name, value):
        """变更一个共享常量。"""
        item = next(d for d in self.data["definitions"] if d["name"] == name)
        item["value"] = value

    def header(self, **fields):
        """默认向 Memory Tile 0 发起一个管理 CPU 读。"""
        result = {field["name"]: 0 for field in self.data["header"]}
        result.update(sender=11, admitted=1, transaction=7, length=64)
        result.update(fields)
        return result

    def target_address(self, x, y, offset=0):
        """从地址窗口公式直接计算全局地址。"""
        return (
            self.memory["TILE_WINDOW_BASE"] + (y * 8 + x) * (1 << 24) + offset
        )

    def test_topology_and_sources(self):
        """24 个路由编号与地址窗口编号分别映射。"""
        for y in range(4):
            for x in range(6):
                node = y * 6 + x
                self.assertEqual(noc.node_id(self.v, x, y), node)
                self.assertEqual(noc.coordinates(self.v, node), (x, y))
                self.assertEqual(
                    noc.window_id(self.data, self.v, node), y * 8 + x
                )
        self.assertEqual(len(self.data["sources"]), 23)
        self.assertEqual(
            [(s["x"], s["y"]) for s in self.data["sources"][:3]],
            [(5, 1), (5, 0), (5, 0)],
        )
        for index in range(16):
            source = self.data["sources"][index + 3]
            self.assertEqual(
                (source["x"], source["y"]), (index % 4 + 1, index // 4)
            )
        for args in ((-1, 0), (6, 0), (0, 4)):
            with self.assertRaises(ValueError):
                noc.node_id(self.v, *args)
        for node in (-1, 24, 32):
            with self.assertRaises(ValueError):
                noc.coordinates(self.v, node)

    def test_dram_interleave_and_boundaries(self):
        """保留完整 DPA，覆盖全部通道及容量末端。"""
        for base in (0, 2048, (32 << 30) - 2048):
            for channel in range(8):
                for offset in (0, 192):
                    address = base + channel * 256 + offset
                    target = noc.decode_address(
                        self.data, self.v, self.dependencies, address, 64
                    )
                    self.assertEqual(target["channel"], channel)
                    self.assertEqual(target["node"], (channel // 2) * 6)
                    self.assertEqual(target["dpa"], address)
        self.assertEqual(
            noc.decode_address(
                self.data, self.v, self.dependencies, (32 << 30) - 1
            )["channel"],
            7,
        )
        for address in (-1, 32 << 30, (64 << 30) - 1, 0x2000000000, 1 << 40):
            with self.subTest(address=address), self.assertRaises(ValueError):
                noc.decode_address(
                    self.data, self.v, self.dependencies, address
                )

    def test_windows_and_reserved_addresses(self):
        """64 个地址窗口的有效节点、保留节点和空洞均有检查。"""
        for y in range(8):
            for x in range(8):
                offset = 0x800000 if x == 0 else 0
                address = self.target_address(x, y, offset)
                if x >= 6 or y >= 4 or (x, y) in ((5, 0), (5, 2), (5, 3)):
                    with self.assertRaises(ValueError):
                        noc.decode_address(
                            self.data, self.v, self.dependencies, address
                        )
                else:
                    target = noc.decode_address(
                        self.data, self.v, self.dependencies, address
                    )
                    self.assertEqual(target["node"], y * 6 + x)
        for x, y in ((1, 0), (5, 1)):
            for offset in ((2 << 20) - 1,):
                noc.decode_address(
                    self.data,
                    self.v,
                    self.dependencies,
                    self.target_address(x, y, offset),
                )
            for offset in (2 << 20, 0x7FFFFF):
                with self.assertRaises(ValueError):
                    noc.decode_address(
                        self.data,
                        self.v,
                        self.dependencies,
                        self.target_address(x, y, offset),
                    )
        for offset in (0, 0x7FFFFF, 0x803000):
            with self.assertRaises(ValueError):
                noc.decode_address(
                    self.data,
                    self.v,
                    self.dependencies,
                    self.target_address(0, 0, offset),
                )

    def test_fragmentation(self):
        """拆分结果连续、无丢失且同时满足两种边界。"""
        self.assertEqual(
            noc.split_request(63, 66), [(63, 1), (64, 64), (128, 1)]
        )
        self.assertEqual(noc.split_request(255, 2), [(255, 1), (256, 1)])
        for address in range(256):
            for size in (1, 2, 8, 64, 128, 512):
                fragments = noc.split_request(address, size)
                current = address
                for start, count in fragments:
                    self.assertEqual(start, current)
                    self.assertEqual(start // 64, (start + count - 1) // 64)
                    self.assertEqual(start // 256, (start + count - 1) // 256)
                    current += count
                self.assertEqual(current, address + size)
        for address, size in (
            (0, 0),
            (0, -1),
            (-1, 1),
            ((1 << 40) - 1, 2),
            (0, True),
        ):
            with self.assertRaises(ValueError):
                noc.split_request(address, size)

    def test_whole_parent_preflight(self):
        """跨通道父请求先完成所有检查，尾部拒绝使整个父请求失败。"""
        args = self.data, self.v, self.dependencies
        self.assertEqual(
            noc.preflight(*args, 1, 224, 64), [(224, 32), (256, 32)]
        )
        with self.assertRaises(ValueError):
            noc.preflight(*args, 1, 4064, 64, protected={0: [(4096, 8192)]})
        self.assertEqual(
            len(
                noc.preflight(
                    *args, 0, 4064, 64, protected={0: [(4096, 8192)]}
                )
            ),
            2,
        )
        with self.assertRaises(ValueError):
            noc.preflight(*args, 1, (32 << 30) - 32, 64)

    def test_source_permissions(self):
        """管理、HDMA、BAR2 和 Compute 的权限矩阵分别生效。"""
        args = self.data, self.v, self.dependencies
        sram = self.target_address(1, 0)
        registers = self.target_address(0, 0, 0x800000)
        for source in (0, 1, 3):
            noc.preflight(*args, source, sram, 64)
        for source in (1, 2, 3):
            with self.assertRaises(ValueError):
                noc.preflight(*args, source, registers, 4)
        noc.preflight(*args, 0, registers, 4)
        with self.assertRaises(ValueError):
            noc.preflight(*args, 2, sram, 4)
        with self.assertRaises(ValueError):
            noc.preflight(*args, 3, 0, 64)
        noc.preflight(*args, 3, 0, 64, exec_regions=[(0, 64)])
        with self.assertRaises(ValueError):
            noc.preflight(*args, 3, 0, 64, exec_regions=[(0, 32), (32, 64)])
        management = self.target_address(5, 1)
        with self.assertRaises(ValueError):
            noc.preflight(*args, 3, management, 8)
        noc.preflight(
            *args,
            3,
            management,
            8,
            management_regions=[(management, management + 8)]
        )
        event = self.target_address(5, 1, 0x800000)
        noc.preflight(*args, 3, event, 4, write=True)
        with self.assertRaises(ValueError):
            noc.preflight(*args, 3, event, 4)
        sync = self.target_address(2, 0, 0x800000)
        noc.preflight(
            *args, 3, sync, 4, write=True, sync_regions=[(sync, sync + 4)]
        )
        with self.assertRaises(ValueError):
            noc.preflight(*args, 3, sync, 4, write=True)

    def test_header_roundtrip(self):
        """固定向量独立核对 128 位小端报头的位序。"""
        header = self.header(
            opcode=1,
            flags=2,
            initiator=1,
            sender=5,
            transaction=0x1234,
            address=0x123456789A,
        )
        raw = bytes.fromhex("81 01 94 34 12 9a 78 56 34 12 40 00 00 00 00 00")
        self.assertEqual(noc.encode_header(self.data, header), raw)
        self.assertEqual(noc.decode_header(self.data, raw), header)
        for field in self.data["header"]:
            if field["name"] == "reserved":
                continue
            one = {field["name"]: (1 << field["bits"]) - 1}
            encoded = noc.encode_header(self.data, one)
            self.assertEqual(
                int.from_bytes(encoded, "little"),
                one[field["name"]] << field["lsb"],
            )
        for mutation in (
            dict(reserved=1),
            dict(length=256),
            dict(opcode=True),
            dict(unknown=0),
        ):
            with self.assertRaises(ValueError):
                noc.encode_header(self.data, mutation)
        with self.assertRaises(ValueError):
            noc.decode_header(self.data, bytes(15))

    def test_packet_lengths_masks_and_padding(self):
        """8 B 字节使能和零填充均占线上的真实字节。"""
        for length in range(1, 65):
            write = self.header(opcode=1, flags=2, length=length)
            payload = bytes(range(length))
            mask = (1 << length) - 1
            raw = noc.encode_packet(self.data, self.v, write, payload, mask, 0)
            self.assertEqual(len(raw), 64 if length <= 40 else 128)
            self.assertEqual(raw[16:24], mask.to_bytes(8, "little"))
            self.assertEqual(raw[24 : 24 + length], payload)
            self.assertEqual(
                noc.decode_packet(self.data, self.v, raw),
                (write, payload, mask),
            )
            response = self.header(
                opcode=8, vn=1, destination=11, sender=0, length=length
            )
            raw = noc.encode_packet(self.data, self.v, response, payload)
            self.assertEqual(len(raw), 64 if length <= 48 else 128)
        partial = self.header(opcode=1, flags=2, address=60, length=4)
        noc.encode_packet(self.data, self.v, partial, b"abcd", 0xF << 60)
        for mask in (1, 1 << 64, -1, None):
            with self.assertRaises(ValueError):
                noc.encode_packet(self.data, self.v, partial, b"abcd", mask)
        raw = noc.encode_packet(self.data, self.v, self.header())
        with self.assertRaises(ValueError):
            noc.decode_packet(self.data, self.v, raw[:-1] + b"\1")
        with self.assertRaises(ValueError):
            noc.decode_packet(self.data, self.v, raw + bytes(64))

    def test_packet_protocol_rejections(self):
        """身份、VN、opcode、长度和多播类别的非法组合均被拒绝。"""
        for changes in (
            dict(opcode=5),
            dict(vn=1),
            dict(sender=5),
            dict(initiator=23),
            dict(destination=17),
            dict(status=1),
            dict(flags=2),
            dict(length=65),
            dict(address=1),
            dict(destinations=1),
            dict(admitted=0),
            dict(flags=1, destinations=2, destination=1),
        ):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                noc.encode_packet(self.data, self.v, self.header(**changes))
        with self.assertRaises(ValueError):
            noc.encode_packet(self.data, self.v, self.header(), bound_source=1)
        command = self.header(opcode=2, vn=2, destination=1)
        noc.encode_packet(
            self.data, self.v, command, bytes(64), bound_source=0
        )
        event = self.header(
            opcode=3, vn=3, destination=11, sender=1, initiator=3, length=4
        )
        noc.encode_packet(self.data, self.v, event, bytes(4), bound_source=3)
        for bad in (
            dict(initiator=1, sender=5),
            dict(destination=0),
            dict(length=8),
        ):
            with self.assertRaises(ValueError):
                noc.encode_packet(
                    self.data, self.v, {**event, **bad}, bytes(4)
                )

    def test_all_xy_routes(self):
        """576 条路由均是相邻最短路径，X 段严格先于 Y 段。"""
        routes = []
        for source in range(24):
            for destination in range(24):
                route = noc.xy_route(self.v, source, destination)
                expected = abs(source % 6 - destination % 6) + abs(
                    source // 6 - destination // 6
                )
                self.assertEqual(len(route), expected + 1)
                self.assertEqual((route[0], route[-1]), (source, destination))
                y_started = False
                for first, second in zip(route, route[1:]):
                    if first // 6 == second // 6:
                        self.assertFalse(y_started)
                        self.assertEqual(abs(first - second), 1)
                    else:
                        y_started = True
                        self.assertEqual(abs(first - second), 6)
                routes.append(route)
        for vn in range(4):
            with self.subTest(vn=vn):
                self.assertEqual(
                    noc.dependency_order(routes),
                    # 水平连续 32 条、垂直连续 24 条、X 到 Y 转向 60 条。
                    dict(channels=76, edges=32 + 24 + 60),
                )
        with self.assertRaises(ValueError):
            noc.dependency_order([[0, 1, 2, 0, 1]])

    def test_multicast_partition(self):
        """遍历全部非空 Compute 子集，分支互斥且精确覆盖目的集合。"""
        compute = [y * 6 + x for y in range(4) for x in range(1, 5)]
        for compact in range(1, 1 << 16):
            mask = sum(
                1 << node
                for bit, node in enumerate(compute)
                if compact & (1 << bit)
            )
            pending = [(11, mask)]
            leaves = 0
            while pending:
                current, remaining = pending.pop()
                branches = noc.multicast_branches(self.v, current, remaining)
                union = 0
                for next_node, subset in branches.items():
                    self.assertFalse(union & subset)
                    union |= subset
                    if next_node == current:
                        self.assertEqual(subset, 1 << current)
                        self.assertFalse(leaves & subset)
                        leaves |= subset
                    else:
                        distance = abs(next_node % 6 - current % 6)
                        distance += abs(next_node // 6 - current // 6)
                        self.assertEqual(distance, 1)
                        pending.append((next_node, subset))
                self.assertEqual(union, remaining)
            self.assertEqual(leaves, mask)
        all_nodes = (1 << 24) - 1
        for current in range(24):
            branches = noc.multicast_branches(self.v, current, all_nodes)
            self.assertEqual(sum(branches.values()), all_nodes)
            self.assertEqual(branches[current], 1 << current)
        mask = (1 << 1) | (1 << 22)
        packet = self.header(
            opcode=1, flags=3, destination=1, destinations=mask
        )
        noc.encode_packet(self.data, self.v, packet, bytes(64), (1 << 64) - 1)
        with self.assertRaises(ValueError):
            noc.encode_packet(
                self.data,
                self.v,
                {**packet, "address": (2 << 20) - 32},
                bytes(64),
                (1 << 64) - 1,
            )

    def test_multicast_completion_results(self):
        """逐目的返回可乱序，汇总只在集合收齐后发生。"""
        for states in itertools.product(range(3), repeat=3):
            rejected = sum(
                1 << i for i, state in enumerate(states) if state == 1
            )
            faulted = sum(
                1 << i for i, state in enumerate(states) if state == 2
            )
            self.assertIsNone(
                noc.aggregate_result(7, 3, rejected & 3, faulted & 3)
            )
            self.assertEqual(
                noc.aggregate_result(7, 7, rejected, faulted), max(states)
            )
        for args in ((0, 0, 0, 0), (3, 4, 0, 0), (3, 1, 2, 0), (3, 3, 1, 1)):
            with self.assertRaises(ValueError):
                noc.aggregate_result(*args)

    def test_control_messages(self):
        """保护同步的来源、载荷和身份均参与校验。"""
        payload = noc.protection_snapshot(
            self.data,
            self.dependencies,
            0,
            1,
            True,
            0,
            3,
            [(0, 0, False)] * 4,
        )
        header = self.header(
            opcode=4, vn=3, initiator=19, sender=0, destination=1
        )
        raw = noc.encode_packet(
            self.data, self.v, header, payload, dependencies=self.dependencies
        )
        self.assertEqual(len(raw), 128)
        self.assertEqual(
            noc.decode_packet(self.data, self.v, raw, self.dependencies)[1],
            payload,
        )
        for changes in (
            dict(initiator=0, sender=11),
            dict(destination=0),
            dict(length=63),
        ):
            with self.assertRaises(ValueError):
                noc.encode_packet(
                    self.data,
                    self.v,
                    {**header, **changes},
                    payload,
                    dependencies=self.dependencies,
                )
        with self.assertRaises(ValueError):
            noc.encode_packet(
                self.data,
                self.v,
                header,
                b"\1" + payload[1:],
                dependencies=self.dependencies,
            )
        quiesce = self.header(opcode=4, vn=3, address=1, destination=1)
        payload = ((1 << 3) | (3 << 24)).to_bytes(64, "little")
        noc.encode_packet(self.data, self.v, quiesce, payload)
        with self.assertRaises(ValueError):
            noc.encode_packet(self.data, self.v, quiesce, bytes(64))

    def test_credit_reservation(self):
        """只为完整包原子预留，并保持可用信用守恒。"""
        for available in range(5):
            for reserved in range(available + 1):
                for packet in (1, 2):
                    result = noc.reserve_credit(available, reserved, packet)
                    if available - reserved >= packet:
                        self.assertEqual(result, reserved + packet)
                        self.assertLessEqual(result, available)
                    else:
                        self.assertIsNone(result)
        for args in ((-1, 0, 1), (4, 5, 1), (4, 0, 0)):
            with self.assertRaises(ValueError):
                noc.reserve_credit(*args)
        reserved = 0
        for _ in range(8):
            reserved = noc.reserve_credit(16, reserved, 2)
        self.assertEqual(reserved, 16)
        self.assertIsNone(noc.reserve_credit(16, reserved, 2))

    def test_resource_costs(self):
        """资源按物理端口及最小一拍包独立手算。"""
        costs = noc.resource_costs(self.data, self.v, self.dependencies)
        self.assertEqual(costs["directed_links"], 76)
        self.assertEqual(costs["input_ports"], 98)
        self.assertEqual(costs["active_NIs"], 22)
        self.assertEqual(costs["requester_NIs"], 18)
        self.assertEqual(costs["data_slots"], 352)
        self.assertEqual(costs["transaction_slots"], 448)
        rows = costs["bytes"]
        self.assertEqual(rows["router_input_data"], 98 * 4 * 4 * 64)
        self.assertEqual(rows["router_input_descriptors"], 98 * 4 * 4 * 16)
        self.assertEqual(rows["router_output_data"], 98 * 64)
        self.assertEqual(rows["NI_receive_data"], 22 * 4 * 8 * 128)
        self.assertEqual(rows["transaction_responses"], 448 * 128)
        self.assertEqual(rows["transaction_metadata"], 448 * 32)
        self.assertEqual(rows["source_CDC_data"], 4096)
        self.assertEqual(rows["protection_mirrors"], 4608)
        self.assertEqual(costs["total_bytes"], sum(rows.values()))
        self.assertEqual(costs["inter_router_signal_bits"], 39520)
        self.assertEqual(costs["local_signal_bits"], 22880)
        self.assertEqual(costs["DDR_owned_FIFO_bytes"], 4096)

    def test_timing_fixed_vectors(self):
        """零至八跳含两个本地链路和末端路由器，控制短包另算。"""
        for hops in range(9):
            self.assertEqual(
                noc.core_round_trip(self.v, hops, 1, 2), (18 + 7 * hops) * 1000
            )
            self.assertEqual(
                noc.core_round_trip(self.v, hops, 2, 1), (18 + 7 * hops) * 1000
            )
            self.assertEqual(
                noc.core_round_trip(self.v, hops, 1, 1), (16 + 6 * hops) * 1000
            )
        self.assertEqual(noc.cdc_bound(4000, 1000, 1, 2), 12000)
        self.assertEqual(noc.cdc_bound(1000, 4000, 1, 2), 18000)
        self.assertEqual(noc.cdc_bound(1000, 949, 1, 2), 5796)
        self.assertEqual(noc.cdc_bound(949, 1000, 3, 2), 9796)
        report = noc.analyse(self.data, self.v, self.dependencies)
        row = next(
            r
            for r in report["unloaded"]
            if r["source"] == "PCIE"
            and r["hops"] == 8
            and r["operation"] == "READ64"
        )
        self.assertEqual(row["end_to_end_ps"], 369592)
        self.assertEqual(report["slow_reference"]["fabric_8hop_ps"], 2960000)

    def test_cdc_phase_boundaries(self):
        """相同边沿也等待下一拍，核验边界前一 tick 和后一 tick。"""
        self.assertEqual(
            [noc.cdc_edges(t, 4, 4, 1, 2) for t in (3, 4, 5)], [24, 28, 28]
        )
        for source, target in ((2, 3), (3, 2), (4, 4), (4, 1)):
            for sp in range(source):
                for tp in range(target):
                    for start in range(12):
                        for beats in (1, 3):
                            finish = noc.cdc_edges(
                                start, source, target, beats, 2, sp, tp
                            )
                            self.assertLessEqual(
                                finish - start,
                                noc.cdc_bound(source, target, beats, 2),
                            )
                            self.assertEqual((finish - tp) % target, 0)
        with self.assertRaises(ValueError):
            noc.cdc_edges(0, 0, 4, 1, 2)

    def test_layered_contract(self):
        """条件性目标与持续拥塞、原生调度上界分别记录。"""
        report = noc.analyse(self.data, self.v, self.dependencies)
        rates = report["bandwidth_GBs"]
        self.assertEqual(rates["full_data_payload_per_link"], 32)
        self.assertEqual(rates["memory_boundary_payload"], 128)
        self.assertEqual(rates["dram_pin_ideal"], 134.928)
        self.assertAlmostEqual(
            rates["HDMA_context_aggregate_at_reference_RTT"], 64 * 64 / 369.592
        )
        self.assertAlmostEqual(
            rates["current_producer_aggregate_at_reference_RTT"],
            16 * 64 / 369.592,
        )
        self.assertEqual(
            report["target_service_bound"], "unproven_native_FRFCFS"
        )
        self.assertEqual(self.data["contracts"]["boundaries"][0], "OFFER")
        self.assertEqual(self.data["contracts"]["boundaries"][-1], "COMPLETE")
        self.assertEqual(len(report["unloaded"]), 81)
        self.assertTrue(
            all(row["end_to_end_ps"] < 1000000 for row in report["unloaded"])
        )

    def test_protection_snapshot(self):
        """快照位序、四区域编码和 ENABLE 屏障分别核对。"""
        raw = noc.protection_snapshot(
            self.data,
            self.dependencies,
            2,
            0x12345678,
            True,
            3,
            1,
            [(4096, 8192, True)] + [(0, 0, False)] * 3,
        )
        self.assertEqual(len(raw), 64)
        self.assertEqual(raw[:5], bytes.fromhex("0278563412"))
        word = int.from_bytes(raw, "little")
        self.assertEqual((word >> 40) & 31, 15)
        self.assertEqual(
            (word >> 45) & ((1 << 81) - 1), 4096 | (8192 << 40) | (1 << 80)
        )
        self.assertEqual(word >> 126, 0)
        for region in (
            (1, 4096, True),
            (4096, 4096, True),
            (0, (32 << 30) + 4096, True),
        ):
            with self.assertRaises(ValueError):
                noc.protection_snapshot(
                    self.data,
                    self.dependencies,
                    0,
                    1,
                    True,
                    0,
                    0,
                    [region] + [(0, 0, False)] * 3,
                )
        self.assertTrue(noc.enable_allowed(7, 7))
        self.assertFalse(noc.enable_allowed(7, 3))
        self.assertFalse(noc.enable_allowed(7, 7, rejected=2))
        self.assertFalse(noc.enable_allowed(7, 7, faulted=4))

    def test_completion_and_ordering(self):
        """真实响应字段逐一匹配，复位代次不进入线上身份。"""
        reply = self.header(opcode=9, vn=1, destination=11, sender=1, length=0)
        request = self.header(opcode=1, flags=2)
        self.assertEqual(noc.match_response(reply, request, 6), 2)
        self.assertEqual(
            noc.match_response({**reply, "sender": 2}, request, 6, received=2),
            6,
        )
        for changes in (
            dict(initiator=1),
            dict(transaction=8),
            dict(sender=3),
            dict(opcode=8),
            dict(address=64),
            dict(length=1),
        ):
            with self.assertRaises(ValueError):
                noc.match_response({**reply, **changes}, request, 6)
        with self.assertRaises(ValueError):
            noc.match_response(reply, request, 6, received=2)
        request = self.header()
        read = self.header(opcode=8, vn=1, destination=11, sender=0)
        self.assertEqual(noc.match_response(read, request, 1), 1)
        with self.assertRaises(ValueError):
            noc.match_response({**read, "length": 63}, request, 1)
        self.assertEqual(
            self.data["contracts"]["tag_reuse"], "after_upstream_acceptance"
        )
        self.assertEqual(
            self.data["contracts"]["fence"],
            "all_prior_transactions_of_same_source",
        )
        self.assertEqual(self.data["contracts"]["media_drain"], "separate")

    def test_lifecycle_contract(self):
        """隔离、FLR 与全 Mesh 复位边界明确，后续关口保持独立。"""
        c = self.data["contracts"]
        self.assertEqual(
            c["quiesce"],
            "close_new_parents_drain_admitted_fragments_and_responses",
        )
        self.assertEqual(c["flr"], "preserve_mesh_retire_only_PCIE_sources")
        self.assertEqual(c["reset"], "whole_mesh_epoch")
        self.assertEqual(
            c["target_timeout"],
            "target_accept_to_target_complete_during_isolation",
        )
        for gate in ("G1", "G2", "G3"):
            self.assertEqual(
                self.data["milestones"][gate]["implementation"], "pending"
            )

    def test_dependency_consistency(self):
        """共享内存与 PCIe 参数直接调用各自校验器。"""
        self.assertEqual(self.memory["TOTAL_BYTES"], 32 << 30)
        self.assertEqual(self.memory["SOURCE_LIMIT"], 16)
        self.assertEqual(self.memory["CK_PS"], 949)
        self.assertEqual(self.dependencies["hdma_default_chunk"], 64)
        self.assertEqual(
            self.dependencies["pcie"]["CONTROLLER_PERIOD_PS"], 4000
        )
        for path, fingerprint in self.dependencies["fingerprints"].items():
            self.assertEqual(
                noc.digest((noc.ROOT / path).read_bytes()), fingerprint
            )

    def test_invalid_capacity_and_definitions(self):
        """缺失、重复、非法容量与不足的位宽使检查失败。"""
        for name, value in (
            ("FLIT_BYTES", True),
            ("FLIT_BYTES", 128),
            ("BYTE_ENABLE_BYTES", 4),
            ("PACKET_SLOT_BYTES", 256),
            ("DATA_SLOTS_HDMA", -1),
            ("INPUT_DEPTH_FLITS", 1),
            ("INPUT_DEPTH_FLITS", 3),
            ("CDC_DEPTH", 3),
            ("CDC_PACKET_BYTES", 64),
            ("TRANSACTION_RESPONSE_BYTES", 64),
            ("TRANSACTION_META_BYTES", 31),
            ("CREDIT_STATE_BITS_PER_VC", 8),
            ("NI_CREDIT_STATE_BITS_PER_VC", 12),
            ("ROUTER_DESCRIPTOR_BYTES", 8),
            ("SIDEBAND_CREDIT_BITS", 2),
            ("NI_CONTROL_BYTES", 128),
            ("MEASURE_PS", 1),
        ):
            with self.subTest(name=name, value=value):
                self.setUp()
                self.change(name, value)
                with self.assertRaises(ValueError):
                    noc.validate(self.data, self.dependencies)
        self.setUp()
        self.data["definitions"].pop()
        with self.assertRaises(ValueError):
            noc.validate(self.data, self.dependencies)
        self.setUp()
        self.data["definitions"].append(self.data["definitions"][0])
        with self.assertRaises(ValueError):
            noc.validate(self.data, self.dependencies)

    def test_invalid_layout_and_contracts(self):
        """静默更换身份、布局、协议或验收口径必须被发现。"""
        mutations = (
            lambda d: d["header"][1].update(lsb=0),
            lambda d: d["sources"][1].update(x=4),
            lambda d: d["flags"].update(MULTICAST=2),
            lambda d: d["decisions"].update(latency="universal_1us"),
            lambda d: d["contracts"].update(target_service_bound="proven"),
            lambda d: d["contracts"].update(tag_reuse="after_send"),
            lambda d: d["coverage"].pop(),
            lambda d: d["acceptance_matrix"].pop(),
            lambda d: d["architecture_aliases"].update(window_columns=6),
        )
        for change in mutations:
            self.setUp()
            change(self.data)
            with self.assertRaises(ValueError):
                noc.validate(self.data, self.dependencies)

    def test_reproducible_outputs_and_types(self):
        """生成物一致，三个编号在 C、C++、Python 中有独立类型。"""
        before = copy.deepcopy(self.data)
        report = noc.analyse(self.data, self.v, self.dependencies)
        outputs = noc.generated_outputs(
            self.data, self.raw, self.v, self.dependencies, report
        )
        for name, content in outputs.items():
            self.assertEqual((noc.ROOT / name).read_text(), content)
        self.assertEqual(self.data, before)
        module_path = noc.ROOT / "gem5-ext/configs/gw100/generated/noc_defs.py"
        spec = importlib.util.spec_from_file_location(
            "generated_noc_defs", module_path
        )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertNotEqual(module.NodeId.X0_Y1, module.WindowId.X0_Y1)
        self.assertNotEqual(module.NodeId.X0_Y0, module.WindowId.X0_Y0)
        self.assertEqual(module.NodeId.X0_Y1.value, 6)
        self.assertEqual(module.WindowId.X0_Y1.value, 8)
        sources = (
            (
                "clang",
                "c",
                '#include "firmware/common/generated/gw100_noc_defs.h"\n'
                "int main(void) { gw_noc_node_id_t n = {6};\n"
                "return n.value != 6; }\n",
            ),
            (
                "clang++",
                "c++",
                '#include "gem5-ext/src/gw100/noc/generated/noc_defs.hh"\n'
                "using namespace gem5::gw100::nocdefs;\n"
                "static_assert(static_cast<unsigned>(NodeId::X0_Y1) == 6);\n"
                "static_assert(static_cast<unsigned>"
                "(WindowId::X0_Y1) == 8);\n",
            ),
        )
        for compiler, language, source in sources:
            result = subprocess.run(
                [
                    compiler,
                    "-x",
                    language,
                    "-fsyntax-only",
                    "-I",
                    str(noc.ROOT),
                    "-",
                ],
                input=source,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_cli_check_report_and_gate(self):
        """正常检查及报告可复现，G1 实现关口明确失败。"""
        command = [sys.executable, str(noc.ROOT / "tools/noc_spec.py")]
        result = subprocess.run(
            command + ["--check"], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run(
            command + ["--gate", "G1"], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 2, result.stderr)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "report.json"
            result = subprocess.run(
                command + ["--check", "--report", str(target)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(target.read_text())
            self.assertEqual(report["kind"], "G0_analytic_reference")
            self.assertEqual(report["profile_sha256"], noc.digest(self.raw))
            self.assertEqual(
                report["tool_sha256"],
                noc.digest((noc.ROOT / "tools/noc_spec.py").read_bytes()),
            )


if __name__ == "__main__":
    unittest.main()
