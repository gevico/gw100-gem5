"""
@file  memory_tile.py
@brief 四 Tile 八通道的独立 timing 组件验证

脚本通过三个硬件入口发出寄存器与数据事务，独立维护字节参考。
@see spec-dram, spec-address-map
"""

import argparse
import json
from pathlib import Path
from types import SimpleNamespace

import m5
from m5.objects import (
    Gw100MemoryProbe,
    Root,
    SrcClockDomain,
    VoltageDomain,
)

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gw100.devices.memory import make_memory_system
from gw100.generated import memory_defs as d


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--mode",
    choices=(
        "single",
        "stress",
        "reset",
        "retention",
        "checkpoint",
        "fault",
        "latency",
    ),
    default="stress",
)
parser.add_argument(
    "--checkpoint-state", choices=("ready", "self-refresh"), default="ready"
)
parser.add_argument("--restore", type=Path)
parser.add_argument("--fail-mask", type=lambda v: int(v, 0), default=0)
parser.add_argument("--timeout-mask", type=lambda v: int(v, 0), default=0)
parser.add_argument("--fault-phase", type=int, choices=(2, 3), default=3)
args = parser.parse_args()

m5.ticks.setGlobalFrequency("1ps")
voltage = VoltageDomain()
probe = Gw100MemoryProbe(
    clk_domain=SrcClockDomain(clock="250MHz", voltage_domain=voltage),
    response_hold="100ns" if args.mode in ("stress", "reset") else "0ns",
)
mgmt = SimpleNamespace(
    tile=probe,
    clk_domain=SrcClockDomain(clock="500MHz", voltage_domain=voltage),
)
system = make_memory_system(
    probe,
    mgmt,
    fail_mask=args.fail_mask,
    timeout_mask=args.timeout_mask,
    fault_phase=args.fault_phase,
)
system.probe = probe
if args.mode == "latency":
    for ctrl in system.controllers:
        ctrl.dram.page_policy = "open"
root = Root(full_system=False, system=system)
program = []


def op(kind, source=0, address=0, size=8, value=0):
    """追加一个有界脚本操作。"""
    program.extend((kind, source, address, size, value))


def reg(tile, offset):
    """计算一个 Tile 的全局寄存器地址。"""
    return (
        d.TILE_WINDOW_BASE
        + tile * d.TILE_WINDOW_STRIDE
        + d.REGISTER_OFFSET
        + offset
    )


def chreg(ch, offset):
    """计算一个通道的寄存器地址。"""
    return reg(ch // 2, d.CHANNEL_BASE + (ch % 2) * d.CHANNEL_STRIDE + offset)


def command(cmd, state):
    """并行发命令并通过 timing 读取确认全部通道。"""
    for ch in range(8):
        op(1, address=chreg(ch, d.CH_COMMAND), size=4, value=cmd)
    op(6)
    for ch in range(8):
        op(5, address=chreg(ch, d.CH_STATE), size=4, value=state)


def initialize():
    """通过软件协议配置和训练，保护表覆盖一个测试区域。"""
    for ch in range(8):
        op(1, address=chreg(ch, d.CH_PROFILE), size=4, value=d.PROFILE_ID)
    command(d.CMD_CONFIGURE, d.STATE_CONFIGURED)
    if args.mode == "fault" and args.fault_phase == d.CMD_CALIBRATE:
        for ch in range(8):
            op(
                1,
                address=chreg(ch, d.CH_COMMAND),
                size=4,
                value=d.CMD_CALIBRATE,
            )
        return
    command(d.CMD_CALIBRATE, d.STATE_CALIBRATED)
    if args.mode == "fault":
        for ch in range(8):
            op(1, address=chreg(ch, d.CH_COMMAND), size=4, value=d.CMD_TRAIN)
        return
    command(d.CMD_TRAIN, d.STATE_TEST)
    op(3, source=1, address=0)
    for tile in range(4):
        op(1, address=reg(tile, d.PROTECTION_BASE), value=0x10000)
        op(1, address=reg(tile, d.PROTECTION_BASE + 8), value=0x11000)
        op(1, address=reg(tile, d.PROTECTION_BASE + 16), size=4, value=1)
        op(1, address=reg(tile, d.REG_PROTECTION_LOCK), size=4, value=1)
    command(d.CMD_ENABLE, d.STATE_READY)


def dpa(channel, local):
    """独立按规格插入通道位。"""
    return (local & 255) | (channel << 8) | ((local >> 8) << 11)


def data_cases():
    """先写全部位置再读回，验证跨 rank、通道和末端无别名。"""
    channels = range(1) if args.mode == "single" else range(8)
    addresses = []
    for ch in channels:
        for rank in range(2):
            for edge in (0, 1):
                local = (
                    (rank << 15)
                    if edge == 0
                    else (((4 << 30) - 64) & ~(1 << 15)) | (rank << 15)
                )
                address = dpa(ch, local)
                addresses.append((1 + ch % 2, address))
                op(
                    1,
                    source=1 + ch % 2,
                    address=address,
                    size=64,
                    value=address ^ 0x1BADCAFEEFA09123,
                )
    op(6)
    for source, address in addresses:
        op(2, source=source, address=address, size=64)
    op(6)
    # 两个部分写进入同一 burst，独立参考验证 RMW 合并。
    op(1, address=0x200000, size=64, value=0xFFFFFFFFFFFFFFFF)
    op(6)
    op(1, source=1, address=0x200003, size=1, value=0x12)
    op(1, source=2, address=0x200008, size=8, value=0xFEEDFACE12345678)
    op(6)
    op(2, source=1, address=0x200000, size=64)
    # 拒绝跨保护边界的整个请求，后续读取验证没有部分写入。
    op(1, address=0xFFC0, size=128, value=0x1122334455667788)
    op(6)
    op(4, source=1, address=0xFFE0, size=64, value=0)
    op(3, source=2, address=0x10000)
    op(3, source=1, address=reg(0, d.REG_VERSION), size=4)
    op(4, address=reg(0, d.PROTECTION_BASE), value=0)
    op(6)
    op(2, address=0xFFC0, size=128)
    op(3, source=1, address=d.TOTAL_BYTES - 4, size=8)
    op(6)


op(16, source=0, address=0)
op(3, source=0, address=0)
op(3, source=1, address=0)
op(4, address=reg(0, d.REG_VERSION), size=64)
op(3, address=reg(0, d.REG_VERSION) + 1, size=4)
op(6)
initialize()
if args.mode == "fault":
    mask = args.fail_mask | args.timeout_mask
    if not mask:
        parser.error("fault 模式需要反馈失败或超时掩码")
    rank_index = (mask & -mask).bit_length() - 1
    channel = rank_index // 2
    op(6)
    if args.timeout_mask:
        op(7, value=d.TRAIN_PS + d.CALIBRATION_PS)
        state = (
            d.STATE_CALIBRATING if args.fault_phase == 2 else d.STATE_TRAINING
        )
        op(11, address=chreg(channel, d.CH_STATE), size=4, value=state)
    else:
        op(5, address=chreg(channel, d.CH_STATE), size=4, value=d.STATE_FAILED)
        op(
            11,
            address=chreg(channel, d.CH_ERROR),
            size=4,
            value=d.ERR_TRAIN | ((rank_index % 2) << 8),
        )
elif args.mode == "latency":
    op(1, address=0x200000, size=64, value=0xABCD)
    op(6)
    command(d.CMD_QUIESCE, d.STATE_QUIESCED)
    command(d.CMD_RESUME, d.STATE_READY)
    op(2, address=0x200000, size=64)
    op(7, value=100_000)
    op(14)
    op(2, address=0x200000, size=64)
    op(6)
else:
    data_cases()
    if args.mode in ("stress", "reset"):
        for i in range(384):
            op(
                1,
                source=i % 3,
                address=dpa(0, 0x100000 + (i << 16)),
                size=64,
                value=0x55AA0000 + i,
            )
        op(6)
        for i in range(384):
            op(2, source=i % 3, address=dpa(0, 0x100000 + (i << 16)), size=64)
        op(6)
        op(1, address=0x400000, size=64, value=0x5555555555555555)
        op(6)
        for i in range(96):
            op(
                1,
                source=1,
                address=0x400000,
                value=0x5555555555555555 if i % 2 else 0xAAAAAAAAAAAAAAAA,
            )
            op(10, source=2, address=0x400000, value=0x5555555555555555)
        op(6)
    if args.mode == "reset":
        for i in range(24):
            op(13, source=i % 3, address=0x500003 + 64 * i, size=1, value=i)
        op(8)
        op(6)
        for tile in range(4):
            op(5, address=reg(tile, d.REG_STATUS), size=4, value=0)
            op(11, address=reg(tile, d.REG_EPOCH), value=1)
        initialize()
        data_cases()
    if args.mode in ("retention", "stress") or (
        args.mode == "checkpoint" and args.checkpoint_state == "self-refresh"
    ):
        # 写请求已完成整请求预检，四个 burst 尚未全部传完即开始排空。
        op(1, source=1, address=0x600040, size=256, value=0x123456789ABCDEF0)
        op(15, value=80_000)
        command(d.CMD_QUIESCE, d.STATE_QUIESCED)
        command(d.CMD_SELF_REFRESH, d.STATE_SELF_REFRESH)
        op(7, value=10_000_000)
    if args.mode == "checkpoint":
        op(9)
    if args.mode in ("retention", "stress") or (
        args.mode == "checkpoint" and args.checkpoint_state == "self-refresh"
    ):
        command(d.CMD_RESUME, d.STATE_READY)
        op(2, source=1, address=0x600040, size=256)
    op(2, source=1, address=0x200000, size=64)
    op(6)

probe.operations = program
m5.instantiate(str(args.restore) if args.restore else None)
event = m5.simulate(20 * d.TIMEOUT_PS)
if event.getCause() == "memory probe measurement":
    m5.stats.reset()
    event = m5.simulate(d.TIMEOUT_PS)
if event.getCause() == "memory probe checkpoint":
    target = Path(m5.options.outdir) / "checkpoint"
    m5.checkpoint(str(target))
    print(f"Memory Tile checkpoint {target}")
elif event.getCause() == "memory probe complete":
    if args.mode != "fault":
        m5.drain()
    m5.stats.dump()
    report = {
        "mode": args.mode,
        "checked_bytes": probe.checks(),
        "source_retries": probe.retries(),
        "ingresses": [
            [i.metric(k) for k in range(7)] for i in system.ingresses
        ],
        "tick": m5.curTick(),
        "bytes": d.TOTAL_BYTES,
    }
    (Path(m5.options.outdir) / "memory-probe.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
    print(f"Memory Tile {args.mode} PASS {probe.checks()} checked bytes")
else:
    raise RuntimeError(f"Memory Tile 测试未完成 {event.getCause()}")
