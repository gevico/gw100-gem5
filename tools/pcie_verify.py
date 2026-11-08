"""
@file  pcie_verify.py
@brief 独立核对 PCIe 原始事件、资源守恒与固定窗口性能

CRC 采用大整数多项式除法，串行时间采用绝对块索引。参考程序不调用
C++ 模型的协议、调度或编码函数。所有结果保存在指定输出目录。
"""

import argparse
import hashlib
import itertools
import json
import pathlib
import shutil
import subprocess
import sys
from collections import Counter

ROOT = pathlib.Path(__file__).resolve().parent.parent
LINK = ROOT / "gem5-ext/src/gw100/pcie/link"
SOURCES = [
    LINK / f"{name}.cc"
    for name in (
        "packet",
        "serializer",
        "flow_control",
        "replay",
        "link_model",
        "link_training",
        "link_checkpoint",
    )
]
FIXTURE = ROOT / "gem5-ext/tests/pcie/link_fixture.cc"
PROFILE = ROOT / "spec-src/pcie/gw100-link.json"


def model_fingerprints():
    """记录原生 PCIe 编译单元、共享定义和头文件。"""
    directory = ROOT / "gem5-ext/src/gw100/pcie"
    paths = sorted([*directory.rglob("*.cc"), *directory.rglob("*.hh")])
    paths += [ROOT / "gem5/src/sim/serialize.hh", PROFILE]
    return {str(path.relative_to(ROOT)): digest(path) for path in paths}


def digest(path):
    """记录真实源码或可执行文件的 SHA-256。"""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wire_crc(data, width, polynomial):
    """把整个输入转成多项式，除以生成多项式后按原文映射字段。"""
    reverse = [int(f"{byte:08b}"[::-1], 2) for byte in data]
    message = int.from_bytes(bytes(reverse), "big")
    dividend = (((1 << width) - 1) << (8 * len(data))) ^ (message << width)
    divisor = (1 << width) | polynomial
    while dividend.bit_length() >= divisor.bit_length():
        dividend ^= divisor << (dividend.bit_length() - divisor.bit_length())
    result = dividend ^ ((1 << width) - 1)
    return bytes(
        int(f"{byte:08b}"[::-1], 2)
        for byte in result.to_bytes(width // 8, "big")
    )


def require(condition, message):
    """任一不符合条件的观测使本轮验收失败。"""
    if not condition:
        raise ValueError(message)


def parse_tlp(raw):
    """只从原始线上 DWORD 提取本轮工作负载使用的字段。"""
    require(len(raw) >= 12 and len(raw) % 4 == 0, "TLP 长度")
    words = [
        int.from_bytes(raw[i : i + 4], "big") for i in range(0, len(raw), 4)
    ]
    fmt = words[0] >> 29
    kind = (words[0] >> 24) & 31
    length = words[0] & 1023 or 1024
    header = 16 if fmt & 1 else 12
    payload = raw[header:] if fmt & 2 else b""
    require(len(raw) == header + len(payload), "TLP 头部长度")
    require(not payload or len(payload) == length * 4, "TLP 载荷长度")
    if 16 <= kind <= 23:
        return dict(
            traffic=0,
            payload=payload,
            requester=words[1] >> 16,
            tag=0,
            message=True,
            config=False,
        )
    if kind == 10:
        return dict(
            traffic=2,
            payload=payload,
            requester=words[2] >> 16,
            tag=(words[2] >> 8) & 255,
            lower=words[2] & 127,
            count=words[1] & 4095 or 4096,
            status=(words[1] >> 13) & 7,
        )
    address = ((words[2] << 32) | words[3]) if fmt & 1 else words[2]
    if kind == 4:
        address &= 4092
    first = words[1] & 15
    last = (words[1] >> 4) & 15
    enabled = [
        i
        for i in range(length * 4)
        if (
            (first if i < 4 else last if i >= length * 4 - 4 else 15)
            >> (i % 4)
        )
        & 1
    ]
    zero_length = length == 1 and not first
    require(bool(enabled) or zero_length, "多 DWORD 请求须有使能字节")
    begin, end = (0, 4) if zero_length else (min(enabled), max(enabled) + 1)
    require(
        (address + begin) // 4096 == (address + end - 1) // 4096,
        "请求跨越 4 KiB",
    )
    return dict(
        traffic=0 if payload and kind != 4 else 1,
        config=kind == 4,
        write=bool(payload),
        payload=payload,
        requester=words[1] >> 16,
        tag=(words[1] >> 8) & 255,
        address=address + begin,
        size=end - begin,
        enabled=enabled,
        raw_address=address,
        zero_length=zero_length,
    )


def data_end(origin, symbols, half_bit, generation):
    """绝对符号索引处的最后位边沿，不逐包累积取整。"""
    if generation < 3:
        return origin + symbols * 10 * half_bit
    blocks, remainder = divmod(symbols, 16)
    bits = blocks * 130 + (2 + remainder * 8 if remainder else 0)
    return origin + bits * half_bit


def check_trace(path, result, profile, native=False):
    """逐事件验证 CRC、相位、信用、顺序、数据和完成匹配。"""
    origins = [0, 0]
    stream_pending = [False, False]
    accepted = {}
    lifecycle = {
        name: set()
        for name in (
            "DLL_COMMIT",
            "RX_COMPLETE",
            "ACK_RETURN",
            "TARGET_VISIBLE",
            "REQUEST_COMPLETE",
        )
    }
    pending = {}
    consumed = [[0] * 6 for _ in range(2)]
    limits = [[None] * 6 for _ in range(2)]
    infinite = [[False] * 6 for _ in range(2)]
    order = [[], []]
    tx_frames = [{}, {}]
    last_serial = [0, 0]
    rx_ack = [[], []]
    last_tick = 0
    counts = Counter()
    half_bits = (800, 400, 250, 125)
    caps = [
        32,
        16,
        16,
        2048,
        256,
        4096,
        32,
        16,
        576,
        2048,
        256,
        32768,
        4096,
        128,
    ]
    ack_table = profile["protocol_values"]["ack_replay_limits"]["ack_symbols"]
    last = None
    damaged_crc = 0
    damaged_dllp = 0
    damaged_framing = 0
    latency_benchmark = (
        native
        and result["generation"] == 4
        and result["width"] == 4
        and not result.get("fault")
        and any(
            pathlib.Path(name).name == "pcie_link.elf"
            for name in result.get("fingerprints", {})
        )
    )
    bars = {}
    active_targets = [{}, {}]
    latency_reads = {}
    bar_latencies = {"bar0": [], "bar2": []}
    boot_latencies = []
    allowed_errors = {
        "": set(),
        "tlp_crc": {0, 3},
        "dllp_crc": {2, 3},
        "drop_tlp": {0, 3},
        "drop_ack": {3, 4},
        "drop_update": set(),
        "framing": {1, 3},
        "duplicate": set(),
    }
    for number, line in enumerate(path.open(), 1):
        event = json.loads(line)
        last = event
        point, side, cookie = (event[x] for x in ("point", "side", "cookie"))
        tick = event["tick"]
        prefix = f"{path.name} 第 {number} 行 {point}"
        require(tick >= last_tick, prefix + " 时间倒退")
        last_tick = tick
        counts[point] += 1
        for endpoint in range(2):
            if f"backend{endpoint}" in event:
                require(
                    0 <= event[f"backend{endpoint}"] <= 64,
                    prefix + " 后端操作槽越界",
                )
            if endpoint == 0 and "backend_np0" in event:
                require(
                    0 <= event["backend_np0"] <= 48,
                    prefix + " RC posted 预留被 NP 占用",
                )
            occupancy = event.get(f"resources{endpoint}")
            if occupancy is None and native:
                continue
            require(
                all(0 <= used <= cap for used, cap in zip(occupancy, caps)),
                prefix + " 实体资源越界",
            )
        raw = bytes.fromhex(event["bytes"])
        generation, width = event["generation"], event["width"]
        half_bit = half_bits[generation - 1]
        if point == "TARGET_BEGIN":
            active_targets[side][cookie] = tick
        if point == "TARGET_END":
            started = active_targets[side].pop(cookie, None)
            if latency_benchmark and side == 1 and started is not None:
                request = accepted[cookie][1]
                base = (bars.get(24, 0) & ~15) | (bars.get(28, 0) << 32)
                if (
                    request["traffic"] == 0
                    and base
                    and base <= request["address"] < base + (2 << 20)
                ):
                    elapsed = tick - started
                    # 此基准的 ROM 装载期为 25 MHz，两端各两级 CDC。
                    require(elapsed >= 88000, prefix + " 启动写早于 ML 确认")
                    boot_latencies.append(elapsed)
        if point == "STATE" and event["value"] in (11, 20):
            stream_pending = [True, True]
        if point == "ORDERED_SET" and stream_pending[side]:
            origins[side] = event["end_half"]
            stream_pending[side] = False
        if point in ("TLP_TX", "DLLP_TX", "SKP"):
            begin, end = event["begin_half"], event["end_half"]
            require(begin >= last_serial[side], prefix + " 串行占用重叠")
            last_serial[side] = end
            if generation < 3:
                stride = 10 * half_bit
                symbols, remainder = divmod(begin - origins[side], stride)
            else:
                block, residue = divmod(begin - origins[side], 130 * half_bit)
                bits, fraction = divmod(residue, half_bit)
                require(fraction == 0 and bits >= 2, prefix + " 块头边界")
                index, remainder = divmod(bits - 2, 8)
                symbols = block * 16 + index
            require(remainder == 0, prefix + " 符号边界")
            if point == "SKP":
                ordered = (40 if generation < 3 else 130) * half_bit
                require(event["ordered_half"] == ordered, prefix + " SKP 长度")
                if generation < 3:
                    expected = begin + ordered
                else:
                    next_block = symbols // 16 + 1
                    if next_block * 16 - symbols < 4 // width:
                        next_block += 1
                    expected = (
                        origins[side] + next_block * 130 * half_bit + ordered
                    )
                require(end == expected, prefix + " EDS/SKP 相位")
                origins[side] += ordered
            else:
                wire_bytes = len(raw) + 2
                expected = data_end(
                    origins[side],
                    symbols + wire_bytes // width,
                    half_bit,
                    generation,
                )
                require(abs(end - expected) <= 2, prefix + " 包尾时序")
                require((begin + 1) // 2 >= tick, prefix + " 包早于仲裁")
        if point == "ACCEPT":
            require(cookie not in accepted, prefix + " 重复接受")
            tlp = parse_tlp(raw)
            accepted[cookie] = (side, tlp, tick)
            order[side].append(cookie)
            if tlp["traffic"] == 1:
                key = side, tlp["requester"], tlp["tag"]
                require(key not in pending, prefix + " tag 重用过早")
                pending[key] = dict(tlp=tlp, received=0, cookie=cookie)
        if point in lifecycle:
            require(cookie in accepted, prefix + " 缺少接受")
            require(cookie not in lifecycle[point], prefix + " 重复观测")
            lifecycle[point].add(cookie)
        if point == "DLL_COMMIT":
            source, tlp, accept_tick = accepted[cookie]
            require(
                tick >= ((accept_tick + 3999) // 4000 + 2) * 4000,
                prefix + " 源适配流水",
            )
            # 基准中只有强排序请求，同 tag 的完成顺序同样保持。
            for older in order[side]:
                if older == cookie:
                    break
                old = accepted[older][1]
                require(old["traffic"] != 0, prefix + " 越过强排序 posted")
                require(
                    not (
                        old["traffic"] == tlp["traffic"] == 2
                        and old["requester"] == tlp["requester"]
                        and old["tag"] == tlp["tag"]
                    ),
                    prefix + " 完成乱序",
                )
            order[side].remove(cookie)
            traffic = tlp["traffic"]
            for index, units in (
                (traffic, 1),
                (traffic + 3, (len(tlp["payload"]) + 15) // 16),
            ):
                require(
                    limits[side][index] is not None, prefix + " 未初始化信用"
                )
                consumed[side][index] += units
                require(
                    infinite[side][index]
                    or consumed[side][index] <= limits[side][index],
                    prefix + " 透支信用",
                )
        if point == "TLP_TX":
            valid_crc = wire_crc(raw[:-4], 32, 0x04C11DB7) == raw[-4:]
            if native and not valid_crc:
                require(
                    result.get("fault") == "tlp_crc"
                    and side == result.get("fault_side")
                    and damaged_crc < result.get("fault_count", 0),
                    prefix + " 非预期 LCRC 错误",
                )
                damaged_crc += 1
            else:
                require(valid_crc, prefix + " LCRC")
            require(
                native or not event["replay"], prefix + " 正常基准出现重放"
            )
            tx_frames[side].setdefault(cookie, []).append(event)
            if generation >= 3:
                token = bytes.fromhex(event["framing"])
                length = (token[0] >> 4) | ((token[1] & 127) << 4)
                # 原文的并行位矩阵含输入位顺序，单独展开四个奇偶集合。
                masks = (0x4D7, 0x6BC, 0x35E, 0x1AF)
                frame_crc = sum(
                    ((length & mask).bit_count() % 2) << bit
                    for bit, mask in enumerate(masks)
                )
                valid = (
                    length * 4 == len(raw) + 2
                    and token[0] & 15 == 15
                    and token[2] >> 4 == frame_crc
                    and token[1] >> 7 == (length & 0x765).bit_count() % 2
                )
                if native and not valid:
                    require(
                        result.get("fault") == "framing"
                        and side == result.get("fault_side")
                        and damaged_framing < result.get("fault_count", 0),
                        prefix + " 非预期 STP 损坏",
                    )
                    damaged_framing += 1
                else:
                    require(valid, prefix + " STP CRC/Parity")
            if latency_benchmark and side == 1:
                completion = parse_tlp(raw[2:-4])
                if completion["traffic"] == 2:
                    key = 0, completion["requester"], completion["tag"]
                    request = pending.get(key)
                    probe = latency_reads.pop(
                        request["cookie"] if request else None, None
                    )
                    if probe and completion["status"] == 0:
                        bar, received = probe
                        elapsed = (event["begin_half"] + 1) // 2 - received
                        limit = 500000 if bar == "bar0" else 1000000
                        require(elapsed <= limit, prefix + f" {bar} 服务超时")
                        bar_latencies[bar].append(elapsed)
        if point == "RX_COMPLETE":
            source = side ^ 1
            candidates = [
                frame
                for frame in tx_frames[source][cookie]
                if tick == (frame["end_half"] + 1) // 2 + 5000
            ]
            require(bool(candidates), prefix + " 传播时延")
            sent = candidates[-1]
            require(raw.hex() == sent["bytes"], prefix + " 传输内容变化")
            rx_ack[side].append((event["value"], tick))
            tlp = parse_tlp(raw[2:-4])
            payload = tlp["payload"]
            if latency_benchmark and side == 1:
                if (
                    tlp.get("config")
                    and tlp.get("write")
                    and tlp["raw_address"] in (16, 24, 28)
                    and len(tlp["enabled"]) == 4
                ):
                    value = int.from_bytes(payload, "little")
                    if value != 0xFFFFFFFF:
                        bars[tlp["raw_address"]] = value
                if (
                    tlp["traffic"] == 1
                    and not tlp.get("config")
                    and not active_targets[1]
                    and len(pending) == 1
                ):
                    for name, offset, size in (
                        ("bar0", 16, 1 << 20),
                        ("bar2", 24, 64 << 20),
                    ):
                        base = bars.get(offset, 0) & ~15
                        if offset == 24:
                            base |= bars.get(28, 0) << 32
                        if base and base <= tlp["address"] < base + size:
                            latency_reads[cookie] = name, tick
            if tlp["traffic"] == 0 and not native:
                for byte in tlp["enabled"]:
                    address = tlp["raw_address"] + byte
                    expected = (
                        address ^ (address >> 8) ^ result["seed"]
                    ) & 255
                    require(payload[byte] == expected, prefix + " posted 数据")
            if tlp["traffic"] == 2:
                key = side, tlp["requester"], tlp["tag"]
                require(key in pending, prefix + " 不存在的完成请求")
                request = pending[key]
                address = request["tlp"]["address"] + request["received"]
                remaining = request["tlp"]["size"] - request["received"]
                config = request["tlp"].get("config", False)
                write = request["tlp"].get("write", False)
                if native and tlp["status"] != 0:
                    require(
                        tlp["status"] in (1, 2, 4) and not payload,
                        prefix + " 失败完成字段",
                    )
                    request["terminal_error"] = True
                    continue
                require(
                    tlp["status"] == 0
                    and tlp["count"]
                    == (
                        4
                        if config
                        else (
                            1
                            if request["tlp"].get("zero_length")
                            else remaining
                        )
                    )
                    and tlp["lower"] == (0 if config else address % 128),
                    prefix + " 完成头字段",
                )
                if config and write:
                    require(not payload, prefix + " 配置写完成含数据")
                    request["received"] = request["tlp"]["size"]
                    continue
                prefix_bytes = address % 4
                returned = min(remaining, len(payload) - prefix_bytes)
                require(returned > 0, prefix + " 空完成")
                for byte in range(returned) if not native else ():
                    location = address + byte
                    expected = (
                        location ^ (location >> 8) ^ result["seed"]
                    ) & 255
                    require(
                        payload[prefix_bytes + byte] == expected,
                        prefix + " completion 数据",
                    )
                request["received"] += returned
        if point == "REQUEST_COMPLETE":
            source, tlp, _ = accepted[cookie]
            key = source, tlp["requester"], tlp["tag"]
            require(
                pending[key].get("terminal_error")
                or pending[key]["received"] == tlp["size"],
                prefix + " 缺失片段",
            )
            del pending[key]
        if point in ("DLLP_TX", "DLLP_RX"):
            valid = wire_crc(raw[:4], 16, 0x100B) == raw[4:]
            if native and not valid:
                require(
                    point == "DLLP_TX"
                    and result.get("fault") == "dllp_crc"
                    and side == result.get("fault_side")
                    and damaged_dllp < result.get("fault_count", 0),
                    prefix + " 非预期 DLLP CRC 错误",
                )
                damaged_dllp += 1
                continue
            require(valid, prefix + " DLLP CRC")
            kind = raw[0]
            if point == "DLLP_TX" and kind == 0:
                acknowledged = ((raw[2] & 15) << 8) | raw[3]
                index = next(
                    (
                        i
                        for i, (seq, _) in enumerate(rx_ack[side])
                        if seq == acknowledged
                    ),
                    None,
                )
                if index is not None:
                    width_index = (1, 2, 4).index(width)
                    maximum = ack_table[str(generation)][
                        str(event.get("mps", result["mps"]))
                    ][width_index]
                    symbol_ps = (4000, 2000, 1000, 500)[generation - 1]
                    for _, received_tick in rx_ack[side][: index + 1]:
                        require(
                            (event["begin_half"] + 1) // 2 - received_tick
                            <= maximum * symbol_ps,
                            prefix + " ACK 超期",
                        )
                    del rx_ack[side][: index + 1]
            group = kind & 0xF0
            if point == "DLLP_RX" and group in (
                0x40,
                0x50,
                0x60,
                0xC0,
                0xD0,
                0xE0,
                0x80,
                0x90,
                0xA0,
            ):
                traffic = (group >> 4) & 3
                values = (
                    ((raw[1] & 63) << 2) | (raw[2] >> 6),
                    ((raw[2] & 15) << 8) | raw[3],
                )
                for index, value, modulus in (
                    (traffic, values[0], 256),
                    (traffic + 3, values[1], 4096),
                ):
                    if limits[side][index] is None:
                        limits[side][index] = value
                        infinite[side][index] = value == 0
                    elif (
                        group in (0x80, 0x90, 0xA0)
                        and not infinite[side][index]
                    ):
                        previous = limits[side][index]
                        limits[side][index] += (value - previous) % modulus
        if point == "ERROR":
            require(
                native
                and event["value"] in allowed_errors[result.get("fault", "")],
                prefix + f" 非预期协议错误 {event['value']}",
            )
    require(last is not None, "空 trace")
    all_cookies = set(accepted)
    for point in ("DLL_COMMIT", "RX_COMPLETE", "ACK_RETURN"):
        require(
            native or lifecycle[point] == all_cookies, f"{point} 未完整收尾"
        )
    require(not pending, "请求账本未排空")
    if not native:
        for side in range(2):
            require(not any(last[f"resources{side}"]), "实体资源未排空")
    if native and result.get("fault") == "tlp_crc":
        require(damaged_crc == result["fault_count"], "有限故障次数不匹配")
    if native and result.get("fault") == "dllp_crc":
        require(damaged_dllp == result["fault_count"], "DLLP 故障次数不匹配")
    if native and result.get("fault") == "framing":
        require(damaged_framing == result["fault_count"], "STP 故障次数不匹配")
    checked = dict(events=sum(counts.values()), counts=dict(counts))
    if latency_benchmark:
        require(all(bar_latencies.values()), "缺失 BAR0/BAR2 孤立服务样本")
        require(boot_latencies, "缺失启动写确认样本")
        checked["bar_service_ps"] = {
            name: dict(samples=len(values), min=min(values), max=max(values))
            for name, values in bar_latencies.items()
        }
        checked["boot_write_service_ps"] = dict(
            samples=len(boot_latencies),
            min=min(boot_latencies),
            max=max(boot_latencies),
        )
    return checked


def throughput_reference(result):
    """由包长、ACK 批次、每包 FC 更新和有限目标槽计算稳态上界。"""
    generation, width = result["generation"], result["width"]
    # 有效原始字节速率，单位 B/ps。128b/130b 块头持续摊销。
    raw = (0.00025, 0.0005, 8 / 8.125 / 1000, 16 / 8.125 / 1000)[
        generation - 1
    ] * width
    skip_fraction = 4 / 1280 if generation < 3 else 1 / 370
    raw *= 1 - skip_fraction
    mps = result["mps"]
    if result["operation"] == "dma":
        completion = min(result["completion"], mps)
        mrrs = result["mrrs"]
        # RC→EP 承载 CplD、对 MRd/MWr 的 ACK 和信用更新。
        a = 1 + 20 / completion + 9 / mrrs
        b = 9 / mps
        # EP→RC 承载 MWr、MRd 头和对 CplD 的 ACK。
        c = 20 / mrrs + 1 / completion
        d = 1 + 20 / mps
        determinant = a * d - b * c
        reads = raw * (d - b) / determinant
        writes = raw * (a - c) / determinant
        slots = (
            reads * result["service_ps"] / mrrs
            + writes * result["write_service_ps"] / mps
        )
        scale = min(1, 64 / slots) if slots else 1
        return [reads * scale, writes * scale]
    if result["operation"] == "write":
        payload = mps
        wire = payload + 20
        if result["duplex"]:
            wire += 8 + 1  # 对面 posted 写的 UpdateFC 与八包累计 ACK。
    else:
        payload = result["mrrs"]
        completion = min(result["completion"], mps)
        pieces = (payload + completion - 1) // completion
        wire = payload + pieces * 20 + 8 + 1
        if result["duplex"]:
            wire += 20 + pieces  # 相反方向的 MRd 和 completion ACK。
    link_rate = raw * payload / wire
    # 一个目标操作占槽至服务结束，读还须保留至最后完成已进入待发队列。
    service = result["service_ps"]
    backend_rate = 64 * payload / service if service else float("inf")
    rate = min(link_rate, backend_rate)
    if result["operation"] == "read" and result["duplex"]:
        rc_backend = 48 * payload / service if service else float("inf")
        return [rate, min(link_rate, rc_backend)]
    return rate


def latency_reference(result):
    """由初始块相位推算孤立 64 B 写的两级流水和目标可见时刻。"""
    probe = result["latency_probe"]
    generation, width = result["generation"], result["width"]
    half_bit = (800, 400, 250, 125)[generation - 1]
    origin = probe["origin_half"]
    committed = ((probe["start_ps"] + 3999) // 4000 + 2) * 4000
    earliest = 2 * committed
    if generation < 3:
        stride = 10 * half_bit
        first_symbol = max(0, (earliest - origin + stride - 1) // stride)
    else:
        block, residue = divmod(max(0, earliest - origin), 130 * half_bit)
        rest = max(0, residue - 2 * half_bit)
        first_symbol = block * 16 + (rest + 8 * half_bit - 1) // (8 * half_bit)
    first_symbol = max(first_symbol, probe["symbols"])
    end_half = data_end(
        origin, first_symbol + 84 // width, half_bit, generation
    )
    received = (end_half + 1) // 2 + 5000
    visible = ((received + 3999) // 4000 + 2) * 4000 + result["service_ps"]
    require(
        abs(probe["visible_ps"] - visible) <= 1,
        "孤立写延迟与独立逐边沿期望不一致",
    )
    return dict(
        expected_ps=visible - probe["start_ps"],
        measured_ps=probe["visible_ps"] - probe["start_ps"],
    )


def build(executable):
    """独立编译纯链路内核，不链接 gem5 系统或既有功能捷径。"""
    compiler = shutil.which("clang++") or shutil.which("c++")
    if not compiler:
        raise RuntimeError("需要 C++17 编译器")
    command = [
        compiler,
        "-std=c++17",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "gem5-ext"),
        str(FIXTURE),
        *(str(path) for path in SOURCES),
        "-o",
        str(executable),
    ]
    subprocess.run(command, check=True)


def scenarios(matrix):
    """十二种速率宽度以及 MPS/MRRS/RCB/完成上限/目标等待组合。"""
    base = [
        (generation, width, "read", 0, 256, 512, 64, 256, 2500000)
        for generation, width in itertools.product(range(1, 5), (1, 2, 4))
    ]
    if not matrix:
        return base
    cases = set(base)
    for generation, width, operation, duplex, mps in itertools.product(
        range(1, 5), (1, 2, 4), ("read", "write"), (0, 1), (128, 256)
    ):
        cases.add(
            (generation, width, operation, duplex, mps, 512, 64, 256, 2500000)
        )
    for mrrs, rcb, completion, service, duplex in itertools.product(
        (128, 256, 512),
        (64, 128),
        (64, 128, 256),
        (0, 250000, 2500000),
        (0, 1),
    ):
        cases.add((4, 4, "read", duplex, 256, mrrs, rcb, completion, service))
    cases.update(dma_scenarios())
    return sorted(cases)


def dma_scenarios():
    """真实 DMA 发包角色，EP 同时发起 MRd 与 MWr。"""
    return [
        (4, 4, "dma", 1, mps, 512, 64, 256, service)
        for mps, service in itertools.product((128, 256), (0, 250000, 2500000))
    ]


def main():
    """保存配置、指纹、trace、独立检查结论及吞吐偏差。"""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[2])
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=ROOT / "build/pcie-link/verification",
    )
    parser.add_argument("--matrix", action="store_true")
    parser.add_argument(
        "--dma-only",
        action="store_true",
        help="只运行 EP 同时 MRd/MWr 的全双工矩阵",
    )
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument(
        "--native",
        type=pathlib.Path,
        help="核对一个原生软件用例输出目录的线上 trace",
    )
    parser.add_argument(
        "--record-build",
        type=pathlib.Path,
        help="为刚构建的原生 gem5 记录源码和二进制指纹",
    )
    args = parser.parse_args()
    if args.record_build:
        executable = args.record_build.resolve()
        executable.with_suffix(".pcie-build.json").write_text(
            json.dumps(
                dict(
                    executable_sha256=digest(executable),
                    sources=model_fingerprints(),
                ),
                indent=2,
            )
            + "\n"
        )
        return 0
    if args.native:
        directory = args.native.resolve()
        result = json.loads((directory / "pcie-report.json").read_text())
        profile = json.loads(PROFILE.read_text())
        checked = check_trace(
            directory / "pcie-link.jsonl", result, profile, native=True
        )
        (directory / "pcie-wire-check.json").write_text(
            json.dumps(checked, indent=2) + "\n"
        )
        print(f"原生 PCIe 线上检查通过，共 {checked['events']} 个事件")
        return 0
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    executable = output / "link_fixture"
    if not args.no_build:
        build(executable)
    profile = json.loads(PROFILE.read_text())
    manifest = dict(
        profile_sha256=digest(PROFILE),
        executable_sha256=digest(executable),
        sources={
            str(p.relative_to(ROOT)): digest(p)
            for p in (
                *SOURCES,
                *sorted(LINK.rglob("*.hh")),
                FIXTURE,
                pathlib.Path(__file__),
            )
        },
        warmup_ps=20000000,
        window_ps=100000000,
        seed=1,
        cases=[],
    )
    failed = 0
    cases = dma_scenarios() if args.dma_only else scenarios(args.matrix)
    for index, case in enumerate(cases):
        trace = output / f"case-{index:03d}.jsonl"
        process = subprocess.run(
            [str(executable), *(str(x) for x in case), str(trace)],
            capture_output=True,
            text=True,
        )
        entry = dict(configuration=case, trace=trace.name)
        try:
            if case[2] == "read" and case[7] < case[6]:
                require(
                    process.returncode == 1
                    and "completion payload below RCB" in process.stderr,
                    "不合法的完成上限与 RCB 组合应明确拒绝",
                )
                entry.update(status="pass", expected_rejection=True)
                manifest["cases"].append(entry)
                print(
                    f"{index:03d} {case} pass expected rejection", flush=True
                )
                continue
            require(process.returncode == 0, process.stderr)
            result = json.loads(process.stdout)
            entry["result"] = result
            entry["trace_check"] = check_trace(trace, result, profile)
            entry["latency_check"] = latency_reference(result)
            expected = throughput_reference(result)
            rates = [value / result["window_ps"] for value in result["bytes"]]
            references = (
                expected
                if isinstance(expected, list)
                else [expected] * (2 if case[3] else 1)
            )
            deviations = [
                rate / reference - 1
                for rate, reference in zip(rates, references)
            ]
            entry.update(
                reference_bytes_per_ps=expected,
                rates_bytes_per_ps=rates,
                deviations=deviations,
            )
            require(
                all(abs(delta) <= 0.05 for delta in deviations),
                f"吞吐偏差 {deviations}",
            )
            entry["status"] = "pass"
        except (ValueError, KeyError, IndexError) as error:
            entry.update(status="fail", error=str(error))
            failed += 1
        manifest["cases"].append(entry)
        print(
            f"{index:03d} {case} {entry['status']} {entry.get('error', '')}",
            flush=True,
        )
        (output / "report.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
        )
    print(
        f"PCIe 独立检查 {len(manifest['cases']) - failed}/{len(manifest['cases'])}"
    )
    return int(failed != 0)


if __name__ == "__main__":
    sys.exit(main())
