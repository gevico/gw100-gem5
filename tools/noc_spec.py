"""
@file  noc_spec.py
@brief 生成 NoC G0 共享定义并核验路由、资源和分层预算

分析使用明确的有限资源及无竞争服务条件。运行期路由器与 NI 由后续
关口实现，分析结果的状态与运行期验证分别记录。

@see spec-noc
"""

import argparse
from collections import defaultdict, deque
import hashlib
import json
from pathlib import Path
import re

from memory_spec import definitions as memory_definitions
from pcie_spec import validate as pcie_definitions

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "spec-src/noc/gw100-noc.json"
REQUIRED = {
    "MESH_COLS",
    "MESH_ROWS",
    "CORE_PERIOD_PS",
    "TICK_PS",
    "FLIT_BYTES",
    "HEADER_BYTES",
    "BYTE_ENABLE_BYTES",
    "MAX_DATA_BYTES",
    "VN_COUNT",
    "VCS_PER_VN",
    "INPUT_DEPTH_FLITS",
    "OUTPUT_DEPTH_FLITS",
    "ROUTER_PIPELINE_CYCLES",
    "NI_PIPELINE_CYCLES",
    "LINK_CYCLES_PER_FLIT",
    "CREDIT_RETURN_CYCLES",
    "PACKET_SLOT_BYTES",
    "ROUTER_DESCRIPTOR_BYTES",
    "CDC_DESCRIPTOR_BYTES",
    "CDC_PACKET_BYTES",
    "CDC_DEPTH",
    "CDC_SYNC_STAGES",
    "NI_RX_PACKETS_PER_VN",
    "DATA_SLOTS_MGMT",
    "DATA_SLOTS_HDMA",
    "DATA_SLOTS_BAR2",
    "DATA_SLOTS_COMPUTE",
    "COMMAND_SLOTS_MGMT",
    "CONTROL_SLOTS_PER_NI",
    "TRANSACTION_REQUEST_BYTES",
    "TRANSACTION_RESPONSE_BYTES",
    "TRANSACTION_META_BYTES",
    "PARENT_DESCRIPTOR_BYTES",
    "PROTECTION_SNAPSHOT_BYTES",
    "REGION_RECORD_BITS",
    "SIDEBAND_FLIT_BITS",
    "SIDEBAND_CREDIT_BITS",
    "CREDIT_STATE_BITS_PER_VC",
    "EGRESS_RR_BITS",
    "INPUT_RR_BITS",
    "ARRIVAL_COUNTER_BITS",
    "END_TO_END_GOAL_PS",
    "REFERENCE_TARGET_PS",
    "WARMUP_PS",
    "MEASURE_PS",
    "SEED",
    "SOURCE_STATE_BYTES",
    "NI_CONTROL_BYTES",
    "EXEC_REGIONS_PER_COMPUTE",
    "MGMT_REGIONS_PER_COMPUTE",
    "PERMISSION_REGION_BITS",
    "NI_CREDIT_STATE_BITS_PER_VC",
}
FACTS = {
    "TOPOLOGY",
    "PACKET",
    "ROUTING",
    "MULTICAST",
    "CREDIT",
    "RESOURCES",
    "TIMING",
    "PERFORMANCE",
    "PROTECTION",
    "ORDERING",
    "LIFECYCLE",
    "DEPENDENCIES",
}


def require(condition, message):
    """任一失配使检查明确失败。"""
    if not condition:
        raise ValueError(message)


def digest(raw):
    """固定输入原件的 SHA-256。"""
    return hashlib.sha256(raw).hexdigest()


def ceil_div(value, divisor):
    """以整数计算向上取整。"""
    return (value + divisor - 1) // divisor


def load_dependencies(data):
    """复用既有校验器取得外部参数，记录实际文件摘要。"""
    files = {name: ROOT / path for name, path in data["dependencies"].items()}
    memory = memory_definitions(json.loads(files["memory"].read_bytes()))
    pcie = pcie_definitions(json.loads(files["pcie"].read_bytes()))
    arch = files["architecture"].read_text()
    address = files["address_map"].read_text()
    clocks = files["clock_reset"].read_text()
    aliases = data["architecture_aliases"]
    require(
        re.search(r"\| `ct.sram_size` \| 2 MiB \|", arch)
        and aliases["ct_sram_bytes"] == 2 << 20,
        "Compute SRAM 容量与总体架构失配",
    )
    require("n = y × 8 + x" in address, "窗口坐标编码失配")
    require("16 MiB" in address, "窗口容量依据失配")
    require("`ct.exec_regions` = 8" in address, "执行区间容量依据失配")
    require(
        memory["TILE_WINDOW_STRIDE"]
        == aliases["window_bytes"] * aliases["window_columns"],
        "Memory Tile 行间距与窗口编号失配",
    )
    require(memory["SOURCE_MGMT"] == 0, "管理来源编码失配")
    require(memory["SOURCE_HDMA"] == 1, "HDMA 来源编码失配")
    require(memory["SOURCE_BAR2"] == 2, "BAR2 来源编码失配")
    require(memory["CHANNELS"] == 8, "通道拓扑失配")
    require(memory["INTERLEAVE_BYTES"] == 256, "交织粒度失配")
    require(memory["BURST_BYTES"] == 64, "原生 burst 容量失配")
    chunk = re.search(
        r"unsigned devChunk = (\d+);", files["hdma_header"].read_text()
    )
    require(chunk is not None, "HDMA 默认子请求大小缺少依据")
    require(
        re.search(
            r"\| `mgmt.cpu_freq` \| 500 MHz \|",
            files["management"].read_text(),
        ),
        "管理时钟依据失配",
    )
    for name, text in (
        ("clock.core_freq", "1 GHz"),
        ("clock.pcie_freq", "250 MHz"),
        ("clock.tile_txn_timeout", "5 µs"),
        ("clock.tile_iso_timeout", "50 µs"),
    ):
        require(
            re.search(
                rf"\| `{re.escape(name)}` \| {re.escape(text)} \|", clocks
            ),
            f"时钟规格失配 {name}",
        )
    return dict(
        memory=memory,
        pcie=pcie,
        hdma_default_chunk=int(chunk[1]),
        fingerprints={
            str(p.relative_to(ROOT)): digest(p.read_bytes())
            for p in files.values()
        },
    )


def node_id(values, x, y):
    """由 Mesh 坐标得到路由编号。"""
    require(
        0 <= x < values["MESH_COLS"] and 0 <= y < values["MESH_ROWS"],
        "坐标越界",
    )
    return y * values["MESH_COLS"] + x


def coordinates(values, node):
    """路由编号还原为坐标。"""
    require(0 <= node < values["MESH_COLS"] * values["MESH_ROWS"], "节点越界")
    return node % values["MESH_COLS"], node // values["MESH_COLS"]


def window_id(data, values, node):
    """由坐标计算地址窗口编号，独立于路由编号。"""
    x, y = coordinates(values, node)
    return y * data["architecture_aliases"]["window_columns"] + x


def xy_route(values, source, destination):
    """生成确定性的先 X 后 Y 路径，包含首末节点。"""
    x, y = coordinates(values, source)
    dx, dy = coordinates(values, destination)
    path = [source]
    while x != dx:
        x += 1 if dx > x else -1
        path.append(node_id(values, x, y))
    while y != dy:
        y += 1 if dy > y else -1
        path.append(node_id(values, x, y))
    return path


def dependency_order(routes):
    """对通道依赖图排序，返回可复核的顶点与边数量。"""
    vertices = set()
    edges = defaultdict(set)
    for path in routes:
        channels = list(zip(path, path[1:]))
        vertices.update(channels)
        for first, second in zip(channels, channels[1:]):
            edges[first].add(second)
    degree = {channel: 0 for channel in vertices}
    for destinations in edges.values():
        for destination in destinations:
            degree[destination] += 1
    pending = deque(sorted(c for c in vertices if not degree[c]))
    ordered = []
    while pending:
        channel = pending.popleft()
        ordered.append(channel)
        for destination in sorted(edges[channel]):
            degree[destination] -= 1
            if not degree[destination]:
                pending.append(destination)
    require(len(ordered) == len(vertices), "通道依赖包含环")
    return dict(channels=len(vertices), edges=sum(map(len, edges.values())))


def multicast_branches(values, current, destinations):
    """将目的位图划分为本地及下一跳的互斥子集。"""
    require(
        0 < destinations < 1 << (values["MESH_COLS"] * values["MESH_ROWS"]),
        "目的掩码越界或为空",
    )
    branches = defaultdict(int)
    for node in range(values["MESH_COLS"] * values["MESH_ROWS"]):
        if destinations & (1 << node):
            path = xy_route(values, current, node)
            next_node = path[1] if len(path) > 1 else current
            branches[next_node] |= 1 << node
    return dict(branches)


def aggregate_result(expected, received, rejected, faulted):
    """按逐目的结果汇总，完成只在全部目的地返回后出现。"""
    require(expected > 0 and not received & ~expected, "完成位图越界")
    require(not (rejected | faulted) & ~received, "结果缺少对应完成")
    require(not rejected & faulted, "一个目的地出现两个终态")
    if received != expected:
        return None
    if faulted:
        return 2
    return 1 if rejected else 0


def split_request(address, size):
    """以 64 B 和 256 B 边界切分已通过完整预检的父请求。"""
    require(type(size) is int and size > 0, "请求长度无效")
    require(type(address) is int and 0 <= address < 1 << 40, "DPA 越界")
    require(address + size <= 1 << 40, "DPA 结束地址越界")
    result = []
    while size:
        count = min(size, 64 - address % 64, 256 - address % 256)
        result.append((address, count))
        address += count
        size -= count
    return result


def decode_address(data, values, dependencies, address, size=1):
    """静态译码一个 NoC 子请求，保留原始 DPA。"""
    require(type(address) is int and 0 <= address < 1 << 40, "DPA 越界")
    require(0 < size <= values["MAX_DATA_BYTES"], "请求长度无效")
    require(address // 64 == (address + size - 1) // 64, "跨 burst 边界")
    memory = dependencies["memory"]
    aliases = data["architecture_aliases"]
    if address < memory["TOTAL_BYTES"]:
        require(address + size <= memory["TOTAL_BYTES"], "越过内存容量")
        channel = (address >> 8) & 7
        return dict(
            kind="dram",
            node=node_id(values, 0, channel >> 1),
            channel=channel,
            dpa=address,
        )
    base = memory["TILE_WINDOW_BASE"]
    relative = address - base
    require(0 <= relative < 64 * aliases["window_bytes"], "保留地址")
    window, offset = divmod(relative, aliases["window_bytes"])
    x, y = (
        window % aliases["window_columns"],
        window // aliases["window_columns"],
    )
    node = node_id(values, x, y)
    require(node not in aliases["reserved_nodes"], "预留节点")
    require(
        node != node_id(values, aliases["pcie_x"], aliases["pcie_y"]),
        "PCIe Tile 数据窗口保留",
    )
    if x == 0:
        require(
            memory["REGISTER_OFFSET"] <= offset
            and offset + size
            <= memory["REGISTER_OFFSET"] + memory["REGISTER_BYTES"],
            "Memory Tile 保留地址",
        )
        kind = "memory_registers"
    elif (x, y) == (aliases["mgmt_x"], aliases["mgmt_y"]):
        require(
            offset + size <= aliases["mgmt_sram_bytes"]
            or 0x800000 <= offset
            and offset + size <= 0x800004,
            "管理 Tile 保留地址",
        )
        kind = "management_sram" if offset < 0x800000 else "management_event"
    else:
        require(
            offset + size <= aliases["ct_sram_bytes"]
            or 0x800000 <= offset
            and offset + size <= aliases["window_bytes"],
            "Compute Tile 保留 SRAM 地址",
        )
        kind = "compute_sram" if offset < 0x800000 else "compute_registers"
    return dict(kind=kind, node=node, offset=offset, dpa=address)


def preflight(
    data,
    values,
    dependencies,
    initiator,
    address,
    size,
    protected=None,
    exec_regions=None,
    management_regions=None,
    sync_regions=None,
    write=False,
):
    """检查完整父请求的区域及保护，结果在第一片产生效果前取得。"""
    require(0 <= initiator < 19, "数据来源无效")
    require(
        len(exec_regions or []) <= values["EXEC_REGIONS_PER_COMPUTE"],
        "执行区间超过容量",
    )
    require(
        len(management_regions or []) <= values["MGMT_REGIONS_PER_COMPUTE"],
        "管理 SRAM 开放区间超过容量",
    )
    fragments = split_request(address, size)
    destinations = [
        decode_address(data, values, dependencies, start, count)
        for start, count in fragments
    ]
    if initiator == 0:
        allowed = {
            "dram",
            "compute_sram",
            "compute_registers",
            "memory_registers",
        }
    elif initiator == 1:
        allowed = {"dram", "compute_sram"}
    elif initiator == 2:
        allowed = {"dram"}
    else:
        allowed = {
            "dram",
            "compute_sram",
            "management_sram",
            "management_event",
            "compute_registers",
        }
    for target, (start, count) in zip(destinations, fragments):
        require(target["kind"] in allowed, "来源区域权限拒绝")
        if target["kind"] == "dram" and initiator != 0:
            for base, limit in (protected or {}).get(target["node"], []):
                require(
                    start + count <= base or start >= limit, "受保护区域拒绝"
                )
        if target["kind"] == "management_sram":
            require(
                any(
                    base <= start and start + count <= limit
                    for base, limit in (management_regions or [])
                ),
                "管理 SRAM 区间未开放",
            )
        if target["kind"] == "management_event":
            require(write and count == 4, "管理事件需要自然对齐的 32 位写")
        if initiator >= 3 and target["kind"] == "compute_registers":
            source = data["sources"][initiator]
            require(
                write
                and target["node"] != node_id(values, source["x"], source["y"])
                and any(
                    base <= start and start + count <= limit
                    for base, limit in (sync_regions or [])
                ),
                "同步寄存器未声明或来源权限拒绝",
            )
    if initiator >= 3 and any(t["kind"] == "dram" for t in destinations):
        require(
            any(
                base <= address and address + size <= limit
                for base, limit in (exec_regions or [])
            ),
            "执行期区间拒绝",
        )
    return fragments


def validate_header(data, values, header, payload_bytes, bound_source=None):
    """核验报文类别及硬件来源绑定，目标功能权限由预检契约处理。"""
    require(
        set(header) == {field["name"] for field in data["header"]},
        "报头字段缺失或多余",
    )
    encode_header(data, header)
    require(header["admitted"] == 1, "线上报文需要已准入标志")
    initiator = header.get("initiator", 0)
    opcode = header.get("opcode", 0)
    require(0 <= initiator < len(data["sources"]), "发起者无效")
    source = data["sources"][initiator]
    source_node = node_id(values, source["x"], source["y"])
    request = next(
        (op for op in data["opcodes"] if op["value"] == opcode), None
    )
    reply = next((op for op in data["opcodes"] if op["reply"] == opcode), None)
    require(request is not None or reply is not None, "保留 opcode")
    require(
        header["vn"] == (request["vn"] if request else 1), "opcode 与 VN 失配"
    )
    destination = header["destination"]
    sender = header["sender"]
    coordinates(values, sender)
    coordinates(values, destination)
    require(
        destination not in data["architecture_aliases"]["reserved_nodes"],
        "目的节点预留",
    )
    if request:
        require(header["status"] == 0, "请求状态必须为零")
        require(sender == source_node, "发送节点与发起者失配")
        if bound_source is not None:
            require(initiator == bound_source, "硬件来源绑定失配")
        require(initiator < 19 or opcode == 4, "Memory 来源仅允许控制")
    else:
        require(destination == source_node, "响应路由与发起者失配")
        require(
            header["status"] in data["status_codes"].values(), "响应状态无效"
        )
    multicast = bool(header["flags"] & data["flags"]["MULTICAST"])
    masked = bool(header["flags"] & data["flags"]["BYTE_ENABLE"])
    require(masked == (opcode == 1), "字节使能标志失配")
    length = header["length"]
    require(length <= values["MAX_DATA_BYTES"], "载荷长度越界")
    if opcode in (0, 1, 2):
        require(length > 0, "数据或指令长度为空")
    if opcode in (0, 1) or opcode == 8 and header["status"] == 0:
        require(header["address"] % 64 + length <= 64, "数据包跨 burst")
    if opcode == 0:
        require(payload_bytes == 0 and not multicast, "读请求格式无效")
    elif opcode == 1:
        require(
            payload_bytes == length + values["BYTE_ENABLE_BYTES"],
            "写载荷长度失配",
        )
    elif opcode == 8 and header["status"] == 0:
        require(payload_bytes == length and length > 0, "读响应长度失配")
    elif reply:
        require(payload_bytes == 0 and length == 0, "完成响应应无载荷")
    else:
        require(payload_bytes == length, "消息载荷长度失配")
    if opcode == 2:
        require(initiator == 0, "指令来源需要管理 NI")
        x, _ = coordinates(values, destination)
        require(1 <= x <= 4, "指令需要 Compute 目的节点")
    if opcode == 3:
        require(
            3 <= initiator < 19
            and destination == node_id(values, 5, 1)
            and length == 4,
            "事件需要 Compute 来源、管理目的及四字节载荷",
        )
    if opcode == 4:
        require(
            length == 64 and header["address"] in range(4), "控制消息格式无效"
        )
        subtype = header["address"]
        if subtype == 0:
            require(19 <= initiator <= 22, "保护快照需要 Memory 控制来源")
            x, _ = coordinates(values, destination)
            require(x != 0, "保护快照目的需要请求者 NI")
        elif subtype in (1, 2):
            require(initiator == 0, "隔离与恢复需要管理控制来源")
        else:
            require(
                destination == 11 and not multicast, "错误报告需要管理目的"
            )
    if multicast:
        require(request is not None and opcode in (1, 2, 4), "多播类别无效")
        mask = header["destinations"]
        allowed_nodes = {
            node_id(values, s["x"], s["y"])
            for s in data["sources"]
            if 3 <= s["id"] < 19
        }
        if opcode == 4:
            allowed_nodes |= {node_id(values, 5, 0), node_id(values, 5, 1)}
        allowed_mask = sum(1 << node for node in allowed_nodes)
        require(mask > 0 and not mask & ~allowed_mask, "多播目的集合无效")
        require(
            destination
            == min(node for node in allowed_nodes if mask & (1 << node)),
            "多播头目的需为最小目的节点",
        )
        if opcode == 1:
            require(
                header["address"] + length
                <= data["architecture_aliases"]["ct_sram_bytes"],
                "多播 SRAM 偏移越界",
            )
    else:
        require(header["destinations"] == 0, "单播目的掩码必须为零")
    return True


def match_response(header, request, expected, received=0):
    """检查真实报头身份及逐目标完成，拒绝重复终态。"""
    require(
        request["opcode"] in range(5)
        and header["opcode"] == request["opcode"] + 8
        and header["initiator"] == request["initiator"]
        and header["transaction"] == request["transaction"]
        and header["destination"] == request["sender"]
        and header["address"] == request["address"]
        and header["status"] in range(3),
        "响应身份失配",
    )
    expected_length = (
        request["length"]
        if request["opcode"] == 0 and header["status"] == 0
        else 0
    )
    require(header["length"] == expected_length, "响应长度失配")
    require(0 <= header["sender"] < 24, "响应节点越界")
    sender = 1 << header["sender"]
    require(expected > 0 and not received & ~expected, "预期响应集合无效")
    require(expected & sender and not received & sender, "迟到或重复响应")
    return received | sender


def protection_snapshot(
    data,
    dependencies,
    tile,
    version,
    locked,
    test_mask,
    ready_mask,
    regions,
):
    """按 64 B 明确定义保护快照，包含四个可禁用区域。"""
    require(
        0 <= tile < 4 and 0 < version < 1 << 32,
        "保护快照身份越界",
    )
    require(
        type(locked) is bool and 0 <= test_mask < 4 and 0 <= ready_mask < 4,
        "保护快照状态越界",
    )
    require(len(regions) == 4, "保护区域数量失配")
    word = tile | (version << 8) | (int(locked) << 40)
    word |= test_mask << 41 | ready_mask << 43
    offset = 45
    for base, limit, enabled in regions:
        require(
            type(enabled) is bool
            and 0 <= base < 1 << 40
            and 0 <= limit < 1 << 40,
            "保护区域编码越界",
        )
        if enabled:
            require(
                base % 4096 == 0
                and limit % 4096 == 0
                and base < limit <= dependencies["memory"]["TOTAL_BYTES"],
                "保护区域范围失配",
            )
        word |= (base | (limit << 40) | (int(enabled) << 80)) << offset
        offset += 81
    return word.to_bytes(64, "little")


def enable_allowed(expected, received, rejected=0, faulted=0):
    """全部请求者 NI 安装快照并返回 OK 后才允许 ENABLE 完成。"""
    return aggregate_result(expected, received, rejected, faulted) == 0


def packet_flits(values, data_bytes=0, write=False):
    """按报头、使能和载荷计算实际占用，包含末拍填充。"""
    require(
        type(data_bytes) is int
        and 0 <= data_bytes <= values["MAX_DATA_BYTES"],
        "载荷长度越界",
    )
    wire_bytes = values["HEADER_BYTES"] + data_bytes
    if write:
        wire_bytes += values["BYTE_ENABLE_BYTES"]
    return ceil_div(wire_bytes, values["FLIT_BYTES"])


def encode_header(data, fields):
    """按共享布局编码 16 B 小端报头，未知字段使检查失败。"""
    names = {item["name"] for item in data["header"]}
    require(set(fields) <= names, "未知报头字段")
    encoded = 0
    for item in data["header"]:
        value = fields.get(item["name"], 0)
        require(
            type(value) is int and 0 <= value < 1 << item["bits"],
            f"字段越界 {item['name']}",
        )
        encoded |= value << item["lsb"]
    require(not fields.get("reserved", 0), "保留字段必须为零")
    return encoded.to_bytes(16, "little")


def decode_header(data, raw):
    """从原始小端报头取出字段，保留字段单独检查。"""
    require(len(raw) == 16, "报头长度失配")
    word = int.from_bytes(raw, "little")
    fields = {
        item["name"]: (word >> item["lsb"]) & ((1 << item["bits"]) - 1)
        for item in data["header"]
    }
    require(fields["reserved"] == 0, "保留字段非零")
    return fields


def encode_packet(
    data,
    values,
    header,
    payload=b"",
    byte_enables=None,
    bound_source=None,
    dependencies=None,
):
    """按 burst 内位号携带字节使能，并以零填充末拍。"""
    body = bytes(payload)
    if header["opcode"] == 1:
        require(
            type(byte_enables) is int and 0 <= byte_enables < 1 << 64,
            "写包缺少八字节使能",
        )
        active = ((1 << header["length"]) - 1) << (header["address"] % 64)
        require(not byte_enables & ~active, "字节使能超出请求范围")
        body = byte_enables.to_bytes(8, "little") + body
    else:
        require(byte_enables is None, "该报文没有字节使能")
    validate_header(data, values, header, len(body), bound_source)
    if header["opcode"] == 4:
        subtype = header["address"]
        word = int.from_bytes(body, "little")
        if subtype == 0:
            require(dependencies is not None, "保护快照检查需要内存档案")
            require(
                body[0] == header["initiator"] - 19, "保护快照的 Tile 身份失配"
            )
            regions = []
            for index in range(4):
                record = word >> (45 + index * 81)
                regions.append(
                    (
                        record & ((1 << 40) - 1),
                        (record >> 40) & ((1 << 40) - 1),
                        bool(record & (1 << 80)),
                    )
                )
            require(
                protection_snapshot(
                    data,
                    dependencies,
                    body[0],
                    (word >> 8) & 0xFFFFFFFF,
                    bool(word & (1 << 40)),
                    (word >> 41) & 3,
                    (word >> 43) & 3,
                    regions,
                )
                == body,
                "保护快照的区域或保留位无效",
            )
        elif subtype in (1, 2):
            source_mask = word & 0xFFFFFF
            mode = (word >> 24) & 0xFF
            require(
                not word >> 32
                and source_mask < 1 << 23
                and 1 <= mode <= 3
                and (not mode & 1 or source_mask > 0),
                "隔离控制载荷无效",
            )
    packet = encode_header(data, header) + body
    size = ceil_div(len(packet), values["FLIT_BYTES"]) * values["FLIT_BYTES"]
    return packet.ljust(size, b"\0")


def decode_packet(data, values, raw, dependencies=None):
    """验证线上的长度与填充，返回真实载荷和字节使能。"""
    require(len(raw) in (64, 128), "包占用必须为一或两个 flit")
    header = decode_header(data, raw[:16])
    opcode = header["opcode"]
    length = header["length"]
    body_bytes = length + 8 if opcode == 1 else length
    require(16 + body_bytes <= len(raw), "包被截断")
    body = raw[16 : 16 + body_bytes]
    validate_header(data, values, header, len(body))
    mask = int.from_bytes(body[:8], "little") if opcode == 1 else None
    payload = body[8:] if opcode == 1 else body
    require(
        encode_packet(
            data, values, header, payload, mask, dependencies=dependencies
        )
        == raw,
        "末拍填充或 flit 数失配",
    )
    return header, payload, mask


def reserve_credit(available, reserved, packet):
    """给完整包预留信用，释放回调在 G1 的真实时序中执行。"""
    require(0 <= reserved <= available and packet > 0, "信用状态无效")
    if available - reserved < packet:
        return None
    return reserved + packet


def cdc_bound(source_ps, destination_ps, beats, stages):
    """相位最坏等待加写拍、同步、读拍的参考上界。"""
    require(min(source_ps, destination_ps, beats, stages) > 0, "CDC 参数无效")
    return (beats + 1) * source_ps + (beats + stages + 1) * destination_ps


def cdc_edges(
    start,
    source_ps,
    destination_ps,
    beats,
    stages,
    source_phase=0,
    destination_phase=0,
):
    """独立相位参考，首拍在可用时刻之后的第一个边沿开始。"""
    require(
        all(
            type(value) is int
            for value in (
                start,
                source_ps,
                destination_ps,
                beats,
                stages,
                source_phase,
                destination_phase,
            )
        )
        and start >= 0
        and min(source_ps, destination_ps, beats, stages) > 0
        and 0 <= source_phase < source_ps
        and 0 <= destination_phase < destination_ps,
        "CDC 边沿参数无效",
    )

    def after(tick, period, phase):
        return ((tick - phase) // period + 1) * period + phase

    write = after(start, source_ps, source_phase) + beats * source_ps
    visible = after(write, destination_ps, destination_phase)
    return visible + (stages + beats) * destination_ps


def core_round_trip(values, hops, request_flits, response_flits, period=None):
    """计入首末本地链路及末端路由器的完整包存储转发。"""
    require(
        0 <= hops <= values["MESH_COLS"] + values["MESH_ROWS"] - 2, "跳数越界"
    )
    period = period or values["CORE_PERIOD_PS"]
    require(
        1 <= request_flits <= 2 and 1 <= response_flits <= 2 and period > 0,
        "包长或周期无效",
    )
    cycles = 4 * values["NI_PIPELINE_CYCLES"]
    cycles += 2 * (hops + 1) * values["ROUTER_PIPELINE_CYCLES"]
    cycles += (
        (hops + 2)
        * (request_flits + response_flits)
        * values["LINK_CYCLES_PER_FLIT"]
    )
    return cycles * period


def resource_costs(data, values, dependencies):
    """逐项计量物理存储及控制线，元数据采用显式预算。"""
    v = values
    cols, rows = v["MESH_COLS"], v["MESH_ROWS"]
    directed = 2 * (rows * (cols - 1) + cols * (rows - 1))
    nis = cols * rows - len(data["architecture_aliases"]["reserved_nodes"])
    ports = directed + nis
    vcs = v["VN_COUNT"] * v["VCS_PER_VN"]
    fifo_entries = ports * vcs * v["INPUT_DEPTH_FLITS"]
    data_slots = sum(source["data_slots"] for source in data["sources"])
    contexts = (
        data_slots + v["COMMAND_SLOTS_MGMT"] + nis * v["CONTROL_SLOTS_PER_NI"]
    )
    requester_nis = len(
        {(s["x"], s["y"]) for s in data["sources"] if s["data_slots"]}
    )
    mem = dependencies["memory"]
    rows_bytes = {
        "router_input_data": fifo_entries * v["FLIT_BYTES"],
        "router_input_descriptors": fifo_entries
        * v["ROUTER_DESCRIPTOR_BYTES"],
        "router_input_sideband": ceil_div(
            fifo_entries * v["SIDEBAND_FLIT_BITS"], 8
        ),
        "router_output_data": ports
        * v["OUTPUT_DEPTH_FLITS"]
        * v["FLIT_BYTES"],
        "router_output_sideband": ceil_div(ports * v["SIDEBAND_FLIT_BITS"], 8),
        "router_pipeline_metadata": ports
        * v["ROUTER_PIPELINE_CYCLES"]
        * v["ROUTER_DESCRIPTOR_BYTES"],
        "router_credit_state": ceil_div(
            (
                directed * v["CREDIT_STATE_BITS_PER_VC"]
                + nis * v["NI_CREDIT_STATE_BITS_PER_VC"]
            )
            * vcs,
            8,
        ),
        "router_arbitration": ceil_div(
            ports * (v["EGRESS_RR_BITS"] + v["INPUT_RR_BITS"])
            + cols * rows * v["ARRIVAL_COUNTER_BITS"],
            8,
        ),
        "NI_receive_data": nis
        * vcs
        * v["NI_RX_PACKETS_PER_VN"]
        * v["PACKET_SLOT_BYTES"],
        "NI_receive_descriptors": nis
        * vcs
        * v["NI_RX_PACKETS_PER_VN"]
        * v["ROUTER_DESCRIPTOR_BYTES"],
        "transaction_requests": contexts * v["TRANSACTION_REQUEST_BYTES"],
        "transaction_responses": contexts * v["TRANSACTION_RESPONSE_BYTES"],
        "transaction_metadata": contexts * v["TRANSACTION_META_BYTES"],
        "parent_descriptors": 19
        * mem["SOURCE_LIMIT"]
        * v["PARENT_DESCRIPTOR_BYTES"],
        "source_CDC_data": 2 * 2 * v["CDC_DEPTH"] * v["CDC_PACKET_BYTES"],
        "source_CDC_metadata": 2
        * 2
        * v["CDC_DEPTH"]
        * v["CDC_DESCRIPTOR_BYTES"],
        "source_CDC_pointers": ceil_div(
            4 * 2 * (v["CDC_DEPTH"].bit_length()) * (1 + v["CDC_SYNC_STAGES"]),
            8,
        ),
        "protection_mirrors": requester_nis
        * mem["TILES"]
        * v["PROTECTION_SNAPSHOT_BYTES"],
        "source_state": len(data["sources"]) * v["SOURCE_STATE_BYTES"],
        "NI_control_and_counters": nis * v["NI_CONTROL_BYTES"],
        "compute_permission_regions": ceil_div(
            16
            * (v["EXEC_REGIONS_PER_COMPUTE"] + v["MGMT_REGIONS_PER_COMPUTE"])
            * v["PERMISSION_REGION_BITS"],
            8,
        ),
    }
    return dict(
        directed_links=directed,
        active_NIs=nis,
        requester_NIs=requester_nis,
        input_ports=ports,
        data_slots=data_slots,
        transaction_slots=contexts,
        bytes=rows_bytes,
        total_bytes=sum(rows_bytes.values()),
        inter_router_signal_bits=directed
        * (
            v["FLIT_BYTES"] * 8
            + v["SIDEBAND_FLIT_BITS"]
            + v["SIDEBAND_CREDIT_BITS"]
        ),
        local_signal_bits=2
        * nis
        * (
            v["FLIT_BYTES"] * 8
            + v["SIDEBAND_FLIT_BITS"]
            + v["SIDEBAND_CREDIT_BITS"]
        ),
        source_CDC_pointer_signal_bits=4 * 2 * v["CDC_DEPTH"].bit_length(),
        DDR_CDC_pointer_signal_bits=mem["CHANNELS"]
        * 2
        * 2
        * mem["FIFO_DEPTH"].bit_length(),
        DDR_owned_FIFO_bytes=mem["CHANNELS"]
        * 2
        * mem["FIFO_DEPTH"]
        * mem["FIFO_BYTES"],
        DDR_owned_metadata_bytes=mem["CHANNELS"]
        * 2
        * mem["FIFO_DEPTH"]
        * v["CDC_DESCRIPTOR_BYTES"],
        DDR_owned_pointer_bytes=ceil_div(
            mem["CHANNELS"]
            * 2
            * 2
            * mem["FIFO_DEPTH"].bit_length()
            * (1 + mem["SYNC_STAGES"]),
            8,
        ),
        accounting="reference_storage_not_synthesized_area",
    )


def validate(data, dependencies):
    """验证首版配置的可实现性、布局、编码及外部契约。"""
    require(data["schema_version"] == 1, "schema 版本无效")
    aliases = data["architecture_aliases"]
    require(
        all(
            type(value) is int
            for name, value in aliases.items()
            if name != "reserved_nodes"
        ),
        "架构映射需要整数",
    )
    require(
        data["architecture_aliases"]
        == dict(
            window_columns=8,
            window_bytes=16777216,
            ct_sram_bytes=2097152,
            mgmt_sram_bytes=2097152,
            pcie_x=5,
            pcie_y=0,
            mgmt_x=5,
            mgmt_y=1,
            reserved_nodes=[17, 23],
        ),
        "架构映射依据失配",
    )
    values = {}
    for item in data["definitions"]:
        name, value = item["name"], item["value"]
        require(
            re.fullmatch(r"[A-Z][A-Z0-9_]*", name) and name not in values,
            "重复或非法常量",
        )
        require(
            type(value) is int and 0 < value < 1 << 64, "常量需要正 uint64"
        )
        require(item["unit"] and item["basis"], "常量缺少单位或依据")
        values[name] = value
    require(set(values) == REQUIRED, "共享参数缺失或多余")
    v = values
    require((v["MESH_COLS"], v["MESH_ROWS"]) == (6, 4), "拓扑失配")
    require(v["TICK_PS"] == 1 and v["CORE_PERIOD_PS"] == 1000, "时间精度失配")
    require(v["FLIT_BYTES"] == 64 and v["HEADER_BYTES"] == 16, "链路口径失配")
    require(v["BYTE_ENABLE_BYTES"] == 8, "字节使能宽度失配")
    require(
        v["PACKET_SLOT_BYTES"] == 128 and v["CDC_PACKET_BYTES"] == 128,
        "固定包槽及源 CDC 宽度失配",
    )
    require(
        v["MAX_DATA_BYTES"] == dependencies["memory"]["BURST_BYTES"],
        "burst 失配",
    )
    require(v["VN_COUNT"] == 4 and v["VCS_PER_VN"] == 1, "VN 配置失配")
    require(
        v["EXEC_REGIONS_PER_COMPUTE"] == 8
        and v["MGMT_REGIONS_PER_COMPUTE"] == 8
        and v["PERMISSION_REGION_BITS"] == 81
        and v["SOURCE_STATE_BYTES"] >= 32
        and v["NI_CONTROL_BYTES"] >= 256,
        "NI 权限或状态预算不足",
    )
    require(
        v["INPUT_DEPTH_FLITS"] & (v["INPUT_DEPTH_FLITS"] - 1) == 0,
        "输入 FIFO 深度需要二次幂",
    )
    require(v["CDC_DEPTH"] & (v["CDC_DEPTH"] - 1) == 0, "CDC 深度需要二次幂")
    require(
        v["INPUT_DEPTH_FLITS"] >= packet_flits(v, v["MAX_DATA_BYTES"], True),
        "完整包信用不足",
    )
    require(v["OUTPUT_DEPTH_FLITS"] == 1, "首版输出寄存器深度失配")
    require(
        v["PACKET_SLOT_BYTES"]
        >= v["HEADER_BYTES"] + v["MAX_DATA_BYTES"] + v["BYTE_ENABLE_BYTES"],
        "完整包槽位不足",
    )
    for name in (
        "TRANSACTION_REQUEST_BYTES",
        "TRANSACTION_RESPONSE_BYTES",
        "CDC_PACKET_BYTES",
    ):
        require(
            v[name] >= v["PACKET_SLOT_BYTES"], f"响应或请求预留不足 {name}"
        )
    require(
        dependencies["pcie"]["CONTROLLER_PERIOD_PS"] == 4000, "PCIe 时钟失配"
    )
    require(v["CDC_SYNC_STAGES"] >= 2, "CDC 同步级数不足")
    require(
        v["SIDEBAND_FLIT_BITS"] >= 3 + (v["VN_COUNT"] - 1).bit_length(),
        "flit 控制线不足",
    )
    require(
        v["SIDEBAND_CREDIT_BITS"] >= 1 + (v["VN_COUNT"] - 1).bit_length(),
        "信用控制线不足",
    )
    require(
        v["CREDIT_STATE_BITS_PER_VC"]
        >= 3 * v["INPUT_DEPTH_FLITS"].bit_length(),
        "信用计数位数不足",
    )
    require(
        v["PACKET_SLOT_BYTES"] % v["FLIT_BYTES"] == 0
        and v["NI_CREDIT_STATE_BITS_PER_VC"]
        >= 3
        * (
            v["NI_RX_PACKETS_PER_VN"]
            * v["PACKET_SLOT_BYTES"]
            // v["FLIT_BYTES"]
        ).bit_length(),
        "NI 固定包槽的信用计数位数不足",
    )
    used = 0
    for field in data["header"]:
        require(
            type(field["bits"]) is int
            and field["bits"] > 0
            and type(field["lsb"]) is int
            and field["lsb"] >= 0,
            "报头布局无效",
        )
        mask = ((1 << field["bits"]) - 1) << field["lsb"]
        require(not used & mask, "报头字段重叠")
        used |= mask
    require(used == (1 << (v["HEADER_BYTES"] * 8)) - 1, "报头覆盖不完整")
    require(
        len({f["name"] for f in data["header"]}) == len(data["header"]),
        "报头字段重名",
    )
    widths = {field["name"]: field["bits"] for field in data["header"]}
    require(
        widths
        == dict(
            opcode=4,
            vn=2,
            flags=2,
            initiator=5,
            destination=5,
            sender=5,
            admitted=1,
            transaction=16,
            address=40,
            length=8,
            destinations=24,
            status=8,
            reserved=8,
        ),
        "首版字段位数失配",
    )
    offset = 0
    for name in (
        "opcode",
        "vn",
        "flags",
        "initiator",
        "destination",
        "sender",
        "admitted",
        "transaction",
        "address",
        "length",
        "destinations",
        "status",
        "reserved",
    ):
        field = next(item for item in data["header"] if item["name"] == name)
        require(field["lsb"] == offset, "首版字段顺序失配")
        offset += field["bits"]
    require(
        v["ROUTER_DESCRIPTOR_BYTES"] >= 16
        and v["PARENT_DESCRIPTOR_BYTES"] >= 64,
        "描述符预算不足",
    )
    require(
        v["EGRESS_RR_BITS"] >= (5 * v["VN_COUNT"] - 1).bit_length()
        and v["INPUT_RR_BITS"] >= 3
        and v["ARRIVAL_COUNTER_BITS"] == 64,
        "仲裁元数据位数不足",
    )
    require(
        sum(bits for _, bits in data["metadata"]["transaction"])
        == 8 * v["TRANSACTION_META_BYTES"],
        "事务元数据预算失配",
    )
    require(
        sum(bits for _, bits in data["metadata"]["snapshot"])
        == 8 * v["PROTECTION_SNAPSHOT_BYTES"],
        "保护快照预算失配",
    )
    require(
        dependencies["memory"]["PROTECTION_ENTRIES"] * v["REGION_RECORD_BITS"]
        == 324,
        "保护区域位数失配",
    )
    require(
        data["metadata"]["transaction"]
        == [
            ["transaction", 16],
            ["expected", 24],
            ["received", 24],
            ["rejected", 24],
            ["faulted", 24],
            ["parent_slot", 8],
            ["state", 8],
            ["epoch", 64],
            ["issue_order", 64],
        ]
        and data["metadata"]["snapshot"]
        == [
            ["tile", 8],
            ["version", 32],
            ["locked", 1],
            ["test_mask", 2],
            ["ready_mask", 2],
            ["regions", 324],
            ["reserved", 143],
        ],
        "元数据字段布局失配",
    )
    require(
        [s["id"] for s in data["sources"]] == list(range(23)), "来源编码不连续"
    )
    require(len({s["name"] for s in data["sources"]}) == 23, "来源名称重复")
    for source in data["sources"]:
        require(
            all(
                type(source[name]) is int
                for name in ("id", "x", "y", "data_slots")
            ),
            "来源字段需要整数",
        )
        node = node_id(v, source["x"], source["y"])
        require(
            node not in data["architecture_aliases"]["reserved_nodes"],
            "来源位于预留节点",
        )
        i = source["id"]
        if i < 3:
            expected = [
                (5, 1, v["DATA_SLOTS_MGMT"]),
                (5, 0, v["DATA_SLOTS_HDMA"]),
                (5, 0, v["DATA_SLOTS_BAR2"]),
            ][i]
        elif i < 19:
            expected = ((i - 3) % 4 + 1, (i - 3) // 4, v["DATA_SLOTS_COMPUTE"])
        else:
            expected = (0, i - 19, 0)
        require(
            (source["x"], source["y"], source["data_slots"]) == expected,
            "来源绑定或额度失配",
        )
    require(
        [(o["value"], o["vn"], o["reply"]) for o in data["opcodes"]]
        == [(0, 0, 8), (1, 0, 9), (2, 2, 10), (3, 3, 11), (4, 3, 12)],
        "opcode 编码失配",
    )
    require(
        data["status_codes"] == dict(OK=0, REJECT=1, FAULT=2), "状态编码失配"
    )
    require(data["flags"] == dict(MULTICAST=1, BYTE_ENABLE=2), "标志编码失配")
    require(
        data["control_messages"]
        == dict(PROTECTION_SYNC=0, QUIESCE=1, RESUME=2, ERROR_REPORT=3),
        "控制消息编码失配",
    )
    require(
        data["control_modes"] == dict(SOURCE=1, TARGET=2), "控制模式编码失配"
    )
    for group in (
        "status_codes",
        "flags",
        "control_messages",
        "control_modes",
    ):
        require(
            all(type(value) is int for value in data[group].values()),
            f"枚举需要整数 {group}",
        )
    for name, expected in dict(
        routing="XY",
        switching="store_and_forward",
        arbitration="oldest_ready_then_round_robin",
        unused_ports="omitted",
    ).items():
        require(data["decisions"][name] == expected, f"参考设计失配 {name}")
    require(
        data["decisions"]["latency"] == "layered"
        and data["decisions"]["link_accounting"] == "header_in_band",
        "已批准决策失配",
    )
    require(
        data["decisions"]["acceptance"] == "cycle_accurate_performance",
        "验收目标失配",
    )
    require(
        data["contracts"]["target_service_bound"] == "unproven_native_FRFCFS",
        "FR-FCFS 上界依据失配",
    )
    require(
        data["contracts"]["boundaries"]
        == [
            "OFFER",
            "ACCEPT",
            "REQ_INJECT",
            "REQ_EJECT",
            "TARGET_ACCEPT",
            "TARGET_COMPLETE",
            "RSP_INJECT",
            "RSP_EJECT",
            "COMPLETE",
        ],
        "分层计时边界失配",
    )
    for name, expected in dict(
        source_identity="hardware_bound",
        tag_reuse="after_upstream_acceptance",
        epoch="internal_lifetime_only",
        response_match="initiator_transaction_expected_sender",
        preflight="whole_parent_before_effects",
        fence="all_prior_transactions_of_same_source",
        write_complete="target_visible_with_forwarding",
        media_drain="separate",
        command_ack="receipt_only",
        NI_ejection_credit="fixed_slot_two_flits_per_packet",
        target_timeout="target_accept_to_target_complete_during_isolation",
        goal_condition="uncontended_normal_clocks_target_250ns_upstream_ready",
        goal_traffic="single_unicast_le_64B",
    ).items():
        require(data["contracts"][name] == expected, f"行为契约失配 {name}")
    require(
        data["contracts"]["aggregate_status"] == ["FAULT", "REJECT", "OK"],
        "多播终态优先级失配",
    )
    require(
        data["contracts"]["fragment_boundaries"] == [64, 256], "分段边界失配"
    )
    require(
        {case["id"] for case in data["acceptance_matrix"]}
        == {
            "UNLOADED",
            "STREAM",
            "HOTSPOT",
            "REFRESH",
            "BACKPRESSURE",
            "MULTICAST",
            "LIFECYCLE",
        },
        "验收矩阵缺项",
    )
    require(
        {f["id"] for f in data["coverage"]} == FACTS
        and len(data["coverage"]) == len(FACTS),
        "覆盖项缺失或重复",
    )
    require(
        all(
            f["state"] == "defined" and f["evidence"] for f in data["coverage"]
        ),
        "覆盖项未定义",
    )
    require(set(data["milestones"]) == {"G0", "G1", "G2", "G3"}, "关口缺失")
    require(
        all(
            m["implementation"] in {"pending", "partial", "complete"}
            for m in data["milestones"].values()
        ),
        "关口状态无效",
    )
    require(
        v["MEASURE_PS"] == 100000000 and v["WARMUP_PS"] == 20000000,
        "测量窗口失配",
    )
    return v


def analyse(data, values, dependencies):
    """计算全部路由、存储成本及正常和低速参考预算。"""
    v = values
    nodes = v["MESH_COLS"] * v["MESH_ROWS"]
    routes = [xy_route(v, s, d) for s in range(nodes) for d in range(nodes)]
    graph = dependency_order(routes)
    core = v["CORE_PERIOD_PS"]
    mem = dependencies["memory"]
    source_periods = dict(
        COMPUTE=None,
        MGMT=2000,
        PCIE=dependencies["pcie"]["CONTROLLER_PERIOD_PS"],
    )
    rows = []
    max_hops = v["MESH_COLS"] + v["MESH_ROWS"] - 2
    for source, period in source_periods.items():
        source_cdc = (
            0
            if period is None
            else (
                cdc_bound(period, core, 1, v["CDC_SYNC_STAGES"])
                + cdc_bound(core, period, 1, v["CDC_SYNC_STAGES"])
            )
        )
        for hops in range(max_hops + 1):
            for op, req, rsp in (
                ("READ64", 1, 2),
                ("WRITE64", 2, 1),
                ("CONTROL64", 2, 1),
            ):
                request_beats = 3 if op == "WRITE64" else 1
                response_beats = 3 if op == "READ64" else 1
                ddr = (
                    0
                    if op == "CONTROL64"
                    else (
                        cdc_bound(
                            core,
                            mem["CK_PS"],
                            request_beats,
                            mem["SYNC_STAGES"],
                        )
                        + cdc_bound(
                            mem["CK_PS"],
                            core,
                            response_beats,
                            mem["SYNC_STAGES"],
                        )
                    )
                )
                total_core = core_round_trip(v, hops, req, rsp)
                total = (
                    source_cdc + total_core + ddr + v["REFERENCE_TARGET_PS"]
                )
                require(
                    total <= v["END_TO_END_GOAL_PS"], "无竞争 1 µs 目标未闭合"
                )
                rows.append(
                    dict(
                        source=source,
                        hops=hops,
                        operation=op,
                        source_CDC_ps=source_cdc,
                        fabric_ps=total_core,
                        DDR_CDC_ps=ddr,
                        target_ps=v["REFERENCE_TARGET_PS"],
                        end_to_end_ps=total,
                    )
                )
    costs = resource_costs(data, v, dependencies)
    payload_rate = (
        v["MAX_DATA_BYTES"]
        / (packet_flits(v, v["MAX_DATA_BYTES"]) * core)
        * 1000
    )
    residence = max(
        row["end_to_end_ps"] for row in rows if row["source"] == "PCIE"
    )
    return dict(
        profile=data["profile"],
        kind="G0_analytic_reference",
        route_pairs=len(routes),
        max_hops=max_hops,
        channel_graphs=[dict(vn=i, **graph) for i in range(v["VN_COUNT"])],
        resources=costs,
        unloaded=rows,
        bandwidth_GBs=dict(
            raw_per_direction=v["FLIT_BYTES"] / core * 1000,
            full_data_payload_per_link=payload_rate,
            memory_boundary_payload=payload_rate * mem["TILES"],
            PCIE_source_CDC_payload=64
            / dependencies["pcie"]["CONTROLLER_PERIOD_PS"]
            * 1000,
            MGMT_source_CDC_payload=64 / 2000 * 1000,
            dram_pin_ideal=mem["DATA_RATE_MT_S"]
            * 16
            / 8
            / 1000
            * mem["CHANNELS"],
            HDMA_context_aggregate_at_reference_RTT=v["DATA_SLOTS_HDMA"]
            * v["MAX_DATA_BYTES"]
            / residence
            * 1000,
            current_producer_aggregate_at_reference_RTT=min(
                v["DATA_SLOTS_HDMA"],
                mem["SOURCE_LIMIT"]
                * ceil_div(
                    dependencies["hdma_default_chunk"], v["MAX_DATA_BYTES"]
                ),
            )
            * v["MAX_DATA_BYTES"]
            / residence
            * 1000,
        ),
        slow_reference=dict(
            core_ps=40000,
            source_ps=40000,
            fabric_8hop_ps=core_round_trip(v, 8, 1, 2, 40000),
            source_CDC_ps=2 * cdc_bound(40000, 40000, 1, v["CDC_SYNC_STAGES"]),
            state="hypothetical_clock_profile_data_requires_ready",
        ),
        dram_reference={
            name: mem[name]
            for name in (
                "CHANNELS",
                "RANKS_PER_CHANNEL",
                "TOTAL_BYTES",
                "BURST_BYTES",
                "DATA_RATE_MT_S",
                "CK_PS",
                "TRFC_PS",
                "TXS_PS",
                "TREFI_PS",
                "READ_SLOTS",
                "WRITE_SLOTS",
                "FRONT_SLOTS",
                "PIPELINE_CYCLES",
            )
        },
        target_service_bound="unproven_native_FRFCFS",
        residence_example_ps=residence,
        residence_example_kind="fixed_residence_sensitivity_not_hard_bound",
        dependencies=dependencies["fingerprints"],
        milestones={
            key: item["implementation"]
            for key, item in data["milestones"].items()
        },
    )


def generated_outputs(data, raw, values, dependencies, report):
    """生成三端常量、覆盖和预算，记录实际来源摘要。"""
    constants = dict(values)
    constants.update(
        WINDOW_COLUMNS=data["architecture_aliases"]["window_columns"],
        TILE_WINDOW_BYTES=data["architecture_aliases"]["window_bytes"],
        TILE_WINDOW_BASE=dependencies["memory"]["TILE_WINDOW_BASE"],
        NODE_COUNT=values["MESH_COLS"] * values["MESH_ROWS"],
        SOURCE_COUNT=len(data["sources"]),
        MAX_PACKET_FLITS=packet_flits(values, 64, True),
        RESERVED_NODE_MASK=sum(
            1 << node
            for node in data["architecture_aliases"]["reserved_nodes"]
        ),
    )
    for name, source_ids in (
        ("COMPUTE", range(3, 19)),
        ("MEMORY", range(19, 23)),
        ("REQUESTER", range(19)),
        ("ACTIVE", range(23)),
    ):
        nodes = {
            node_id(values, s["x"], s["y"])
            for s in data["sources"]
            if s["id"] in source_ids
        }
        constants[f"{name}_NODE_MASK"] = sum(1 << node for node in nodes)
    for field in data["header"]:
        constants[f"HEADER_{field['name'].upper()}_LSB"] = field["lsb"]
        constants[f"HEADER_{field['name'].upper()}_BITS"] = field["bits"]
    for group, fields in data["metadata"].items():
        offset = 0
        for name, bits in fields:
            prefix = f"{group.upper()}_{name.upper()}"
            constants[f"{prefix}_LSB"] = offset
            constants[f"{prefix}_BITS"] = bits
            offset += bits
    for source in data["sources"]:
        constants[f"SOURCE_{source['name']}"] = source["id"]
        constants[f"SOURCE_{source['name']}_NODE"] = node_id(
            values, source["x"], source["y"]
        )
    for op in data["opcodes"]:
        constants[f"OP_{op['name']}"] = op["value"]
        constants[f"OP_{op['name']}_RESPONSE"] = op["reply"]
    for name, value in data["status_codes"].items():
        constants[f"STATUS_{name}"] = value
    for group in ("flags", "control_messages", "control_modes"):
        for name, value in data[group].items():
            constants[f"{group.upper()}_{name}"] = value
    for y in range(values["MESH_ROWS"]):
        for x in range(values["MESH_COLS"]):
            constants[f"NODE_X{x}_Y{y}"] = node_id(values, x, y)
            constants[f"WINDOW_X{x}_Y{y}"] = window_id(
                data, values, node_id(values, x, y)
            )
    stamp = digest(raw)
    c = [
        "/**",
        " * @file  gw100_noc_defs.h",
        " * @brief NoC G0 共享定义，请勿手改",
        " * gw100-noc.json SHA-256",
        f" * {stamp}",
        " * @see spec-noc",
        " */",
        "#ifndef GW100_NOC_DEFS_H",
        "#define GW100_NOC_DEFS_H",
        "#include <stdint.h>",
        "",
    ]
    cc = [
        "/**",
        " * @file  noc_defs.hh",
        " * @brief NoC G0 共享定义，请勿手改",
        " * gw100-noc.json SHA-256",
        f" * {stamp}",
        " * @see spec-noc",
        " */",
        "#ifndef GW100_NOC_DEFS_HH",
        "#define GW100_NOC_DEFS_HH",
        "#include <cstdint>",
        "",
        "namespace gem5::gw100::nocdefs {",
        "",
    ]
    py = [
        '"""',
        "@file  noc_defs.py",
        "@brief NoC G0 共享定义，请勿手改",
        "gw100-noc.json SHA-256",
        stamp,
        "@see spec-noc",
        '"""',
        "",
        "from enum import Enum",
        "",
    ]
    for name, value in constants.items():
        c.append(f"#define GW_NOC_{name} UINT64_C({value})")
        cc.append(f"inline constexpr uint64_t {name} = {value}ULL;")
        py.append(f"{name} = {value}")
    for name in ("node", "window", "source"):
        c.extend(
            [
                "",
                f"typedef struct {{ uint8_t value; }} gw_noc_{name}_id_t;",
            ]
        )
    for typename, member_values in (
        (
            "NodeId",
            [
                (f"X{x}_Y{y}", node_id(values, x, y))
                for y in range(values["MESH_ROWS"])
                for x in range(values["MESH_COLS"])
            ],
        ),
        (
            "WindowId",
            [
                (f"X{x}_Y{y}", window_id(data, values, node_id(values, x, y)))
                for y in range(values["MESH_ROWS"])
                for x in range(values["MESH_COLS"])
            ],
        ),
        ("SourceId", [(s["name"], s["id"]) for s in data["sources"]]),
    ):
        cc.extend(["", f"enum class {typename} : uint8_t {{"])
        py.extend(["", f"class {typename}(Enum):"])
        py.append(f'    """独立的 {typename} 编号。"""')
        py.append("")
        for name, value in member_values:
            cc.append(f"    {name} = {value},")
            py.append(f"    {name} = {value}")
        cc.append("};")
        py.append("")
    c.extend(["", "#endif", ""])
    cc.extend(["", "} // namespace gem5::gw100::nocdefs", "#endif", ""])
    py.append("")
    coverage = [
        "# NoC G0 覆盖矩阵",
        "",
        f"档案摘要为 `{digest(raw)}`。",
        "",
        "| 项 | 定义 | 状态 | 检查 |",
        "|---|---|---|---|",
    ]
    for fact in data["coverage"]:
        coverage.append(
            f"| {fact['id']} | {fact['title']} | {fact['state']} | "
            f"{fact['evidence']} |"
        )
    coverage += [
        "",
        "## 实施关口",
        "",
        "| 关口 | 状态 | 证据 |",
        "|---|---|---|",
    ]
    for gate, state in data["milestones"].items():
        coverage.append(
            f"| {gate} | {state['implementation']} | "
            f"{'、'.join(state['evidence'])} |"
        )
    coverage += [
        "",
        "## 报头布局",
        "",
        "位 0 先占据小端字节的最低位。",
        "",
        "| 字段 | LSB | 位数 |",
        "|---|---|---|",
    ]
    for field in data["header"]:
        coverage.append(
            f"| {field['name']} | {field['lsb']} | {field['bits']} |"
        )
    coverage += [
        "",
        "## 共享参数",
        "",
        "| 参数 | 值 | 单位 | 依据 |",
        "|---|---|---|---|",
    ]
    for item in data["definitions"]:
        coverage.append(
            f"| {item['name']} | {item['value']} | {item['unit']} | "
            f"{item['basis']} |"
        )
    coverage.append("")
    budget = [
        "# NoC G0 参考预算",
        "",
        f"档案摘要为 `{digest(raw)}`。数值均为参考设计推算。",
        "",
        "## 资源",
        "",
        "| 存储项目 | 字节 |",
        "|---|---|",
    ]
    for name, count in report["resources"]["bytes"].items():
        budget.append(f"| {name} | {count} |")
    resource = report["resources"]
    budget += [
        f"| 合计 | {resource['total_bytes']} |",
        "",
        f"有效 NI {resource['active_NIs']} 个，"
        f"请求者 NI {resource['requester_NIs']} 个，"
        f"输入端口 {resource['input_ports']} 个。",
        f"数据子请求槽 {resource['data_slots']} 个，"
        f"总事务槽 {resource['transaction_slots']} 个。",
        f"路由器间控制及数据线合计 {resource['inter_router_signal_bits']} bit，"
        f"本地端口 {resource['local_signal_bits']} bit。",
        "源 CDC 跨域灰码指针合计 "
        f"{resource['source_CDC_pointer_signal_bits']} bit，DDR CDC 为 "
        f"{resource['DDR_CDC_pointer_signal_bits']} bit。",
        f"DDR 所属 FIFO 数据存储为 {resource['DDR_owned_FIFO_bytes']} B，"
        "另列于 Memory Tile 成本。",
        f"DDR FIFO 描述符参考预算为 {resource['DDR_owned_metadata_bytes']} B，"
        f"读写指针及同步为 {resource['DDR_owned_pointer_bytes']} B。",
        "",
        "计数为显式存储和布线位数，综合面积、布线长度和功耗由后续实现核验。",
        "",
        "## 无竞争最大距离预算",
        "",
        "目标服务取 250 ns。源端等待、返回反压和竞争分别另记。",
        "",
        "| 来源 | 报文 | 跳数 | 源 CDC ps | Mesh 与 NI ps | DDR CDC ps | 端到端 ps |",
        "|---|---|---|---|---|---|---|",
    ]
    for row in report["unloaded"]:
        if row["hops"] == report["max_hops"]:
            budget.append(
                f"| {row['source']} | {row['operation']} | {row['hops']} | "
                f"{row['source_CDC_ps']} | {row['fabric_ps']} | "
                f"{row['DDR_CDC_ps']} | {row['end_to_end_ps']} |"
            )
    budget += ["", "## 带宽与资源驻留约束", "", "| 项 | GB/s |", "|---|---|"]
    for name, rate in report["bandwidth_GBs"].items():
        budget.append(f"| {name} | {rate:.6f} |")
    budget += [
        "",
        "完整 64 B 载荷占两拍，单链路载荷上界为 32 GB/s，四个 Memory Tile 边界合计 128 GB/s。",
        "HDMA 槽位速率按 64 个子请求槽和固定参考驻留时间推算，两个方向共享这些槽。",
        "当前 HDMA 默认每次只发 64 B，现有每来源 16 个父事务额度会进一步限制为 16 个子请求在途。",
        "表中同时列出这两种驻留示例，64 个 NoC 槽的接入效果由 G2 验证。",
        "参考驻留时间取八跳相位包络，N×64 B/T 仅用于固定驻留敏感性分析。实际吞吐上界需要实际平均驻留时间或独立证明的最小驻留约束。",
        "父事务大小、分段并行度、控制流量、反压和目标服务还会影响实际吞吐。",
        "",
        "## 当前 DRAM 档案",
        "",
        "| 参数 | 值 |",
        "|---|---|",
    ]
    for name, value in report["dram_reference"].items():
        budget.append(f"| {name} | {value} |")
    budget += [
        "",
        "带 PS 后缀的参数单位为 ps。控制器前后端流水各为 PIPELINE_CYCLES 个 CK，计入目标服务。",
        "原生分段 burst 的各参数分别量化，完整时序由 LPDDR5.py 及内存规格拥有。",
        "",
        "## 低速参考",
        "",
        "假设核心及来源均为 25 MHz，八跳 Mesh 读写往返为 "
        f"{report['slow_reference']['fabric_8hop_ps']} ps，源端 CDC 为 "
        f"{report['slow_reference']['source_CDC_ps']} ps。",
        "此行用于区分时钟档位，当前固件释放 NoC 的真实时序仍由时钟复位规格拥有。",
        "运行中低速档和未初始化目标各自受准入条件约束。",
        "",
        "## 验证层次",
        "",
        f"已检查 {report['route_pairs']} 个源目的组合及四个独立 VN 的通道依赖图。",
        "通道图分析与响应预留规则共同进入 G1 的协议进展验证。",
        "原生 FR-FCFS 的目标服务最坏上界保持未证明状态。",
        "G1 周期实测、G2 接入和 G3 整机性能各自保留独立关口。",
        "",
    ]
    return {
        "firmware/common/generated/gw100_noc_defs.h": "\n".join(c),
        "gem5-ext/src/gw100/noc/generated/noc_defs.hh": "\n".join(cc),
        "gem5-ext/configs/gw100/generated/noc_defs.py": "\n".join(py).rstrip() + "\n",
        "docs/spec/interconnect/spec-noc/coverage.md": "\n".join(coverage),
        "docs/spec/interconnect/spec-noc/budget.md": "\n".join(budget),
    }


def main():
    """执行生成、独立检查或输出可复核的 G0 报告。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--status", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--gate", choices=("G0", "G1", "G2", "G3"))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        raw = PROFILE.read_bytes()
        data = json.loads(raw)
        dependencies = load_dependencies(data)
        values = validate(data, dependencies)
        report = analyse(data, values, dependencies)
        report["profile_sha256"] = digest(raw)
        report["tool_sha256"] = digest(Path(__file__).read_bytes())
        report["tests_sha256"] = digest(
            (ROOT / "tools/noc_spec_test.py").read_bytes()
        )
        outputs = generated_outputs(data, raw, values, dependencies, report)
        if args.check or args.gate:
            for name, content in outputs.items():
                require(
                    (ROOT / name).is_file()
                    and (ROOT / name).read_text() == content,
                    f"NoC 生成文件需要更新 {name}",
                )
        elif not (args.status or args.json or args.report):
            for name, content in outputs.items():
                path = ROOT / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(
                json.dumps(report, ensure_ascii=False, indent=2) + "\n"
            )
        if args.json:
            print(json.dumps(report, ensure_ascii=False, indent=2))
        else:
            print(
                f"NoC G0 分析通过，{report['route_pairs']} 个路径，四个 VN 无通道环"
            )
            for name, state in report["milestones"].items():
                print(name, state)
        if args.gate:
            for name in ("G0", "G1", "G2", "G3"):
                state = data["milestones"][name]
                if (
                    state["implementation"] != "complete"
                    or not state["evidence"]
                ):
                    return 2
                require(
                    all((ROOT / path).is_file() for path in state["evidence"]),
                    f"关口证据缺失 {name}",
                )
                if name == args.gate:
                    break
        return 0
    except (KeyError, TypeError, ValueError, OSError) as error:
        parser.exit(1, f"NoC G0 检查失败 {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
