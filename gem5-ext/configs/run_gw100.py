"""
@file  run_gw100.py
@brief GW100 仿真的入口脚本：组合板卡、host 和设备

用法（在 gem5 目录执行）：
    ./build/GW100/gem5.opt ../gem5-ext/configs/run_gw100.py \
        --board arm_vexpress --host timing --device gw100 \
        --workload baremetal \
        --kernel /path/to/hello.elf

板卡（gw100/boards/）、host 预设（gw100/hosts/）和设备预设
（gw100/devices/）互相独立，只在这里组合；设备要求的能力板卡
不提供时直接报错。默认是 bare-metal，不启动 Linux、不生成 DTB，
也不使用 bootloader。

裸机程序用 semihosting 的 SYS_EXIT（ADP_Stopped_ApplicationExit）退出，
子码就是本脚本的进程退出码：0 通过、1 失败、2 异常（见 baremetal/README.md）。
到达 --max-ticks 时退出码为 124，其他退出原因为 125。

调试裸机程序：给 gem5 加 --listener-mode=on，给本脚本加 --wait-gdb，
gem5 会在仿真开始前等待 GDB 连接 127.0.0.1:7100（--gdb-port 可改）。
不用 gem5 默认的 7000：macOS 的"隔空播放接收器"占用了 *:7000。
用法见 baremetal/README.md 的"调试"。

@see man-gem5-user, spec-pcie
"""

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

import m5

from gem5.simulate.simulator import Simulator

from gw100.boards import BOARDS, DEFAULT_BOARD
from gw100.devices import DEFAULT_DEVICE, DEVICES
from gw100.devices.memory import make_memory_system
from gw100.generated import memory_defs as d
from gw100.hosts import DEFAULT_HOST, HOSTS
from gw100.workload import set_linux_workload

# 启动 ROM 的默认镜像：make firmware-build 的输出
DEFAULT_ROM = os.path.normpath(
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..",
        "..",
        "build",
        "gw100-firmware",
        "rom.elf",
    )
)

APP_EXIT = "semi:ADP_Stopped_ApplicationExit"
EXIT_TICK_LIMIT = 124
EXIT_OTHER = 125


class Args(argparse.Namespace):
    """命令行参数；字段的类型供静态检查使用。"""

    board: str
    host: str
    device: str
    workload: str
    kernel: str | None
    max_ticks: int
    wait_gdb: bool
    gdb_port: int
    rom: str
    dram_fail_mask: int
    dram_timeout_mask: int
    dram_fault_phase: str
    dram_corrupt_selftest: bool
    dram_corrupt_mask: int
    hardware_reset_test: bool


def main() -> None:
    m5.ticks.setGlobalFrequency("1ps")
    parser = argparse.ArgumentParser(description="GW100 board/host/device")
    _ = parser.add_argument(
        "--board",
        choices=sorted(BOARDS),
        default=DEFAULT_BOARD,
        help=f"板卡，见 gw100/boards/（默认 {DEFAULT_BOARD}）",
    )
    _ = parser.add_argument(
        "--host",
        choices=sorted(HOSTS),
        default=DEFAULT_HOST,
        help=f"host 预设，见 gw100/hosts/（默认 {DEFAULT_HOST}）",
    )
    _ = parser.add_argument(
        "--device",
        choices=sorted(DEVICES),
        default=DEFAULT_DEVICE,
        help=f"设备预设，见 gw100/devices/（默认 {DEFAULT_DEVICE}）",
    )
    _ = parser.add_argument(
        "--workload",
        choices=["baremetal", "linux"],
        default="baremetal",
        help="默认 baremetal；linux 仅用于对比",
    )
    _ = parser.add_argument(
        "--kernel",
        help="bare-metal ELF 路径（--workload baremetal 时必填）",
    )
    _ = parser.add_argument(
        "--max-ticks",
        type=int,
        default=0,
        help="裸机仿真的 tick 上限，0 表示不限（默认）；用于防止程序死循环",
    )
    _ = parser.add_argument(
        "--wait-gdb",
        action="store_true",
        help="仿真开始前等待远程 GDB 连接，用于调试裸机程序；"
        + "需同时给 gem5 加 --listener-mode=on",
    )
    _ = parser.add_argument(
        "--gdb-port",
        type=int,
        default=7100,
        help="远程 GDB 端口（默认 7100；macOS 的隔空播放占用了 7000）",
    )
    _ = parser.add_argument(
        "--rom",
        default=DEFAULT_ROM,
        help=f"管理 Tile 启动 ROM 的 RISC-V 镜像（默认 {DEFAULT_ROM}）",
    )
    parser.add_argument(
        "--dram-fail-mask", type=lambda v: int(v, 0), default=0
    )
    parser.add_argument(
        "--dram-timeout-mask", type=lambda v: int(v, 0), default=0
    )
    parser.add_argument(
        "--dram-fault-phase", choices=("calibrate", "train"), default="train"
    )
    parser.add_argument("--dram-corrupt-selftest", action="store_true")
    parser.add_argument(
        "--dram-corrupt-mask", type=lambda v: int(v, 0), default=0
    )
    parser.add_argument(
        "--hardware-reset-test",
        action="store_true",
        help="允许裸机测试请求外部数字复位输入",
    )
    parser.add_argument(
        "--pcie-gen", type=int, choices=(1, 2, 3, 4), default=4
    )
    parser.add_argument("--pcie-width", type=int, choices=(1, 2, 4), default=4)
    parser.add_argument("--pcie-rcb", type=int, choices=(64, 128), default=64)
    parser.add_argument(
        "--pcie-cpl", type=int, choices=(64, 128, 256), default=256
    )
    parser.add_argument("--pcie-rc-aspm", action="store_true")
    parser.add_argument("--pcie-trace", action="store_true")
    parser.add_argument("--pcie-host-service", default="0ns")
    parser.add_argument(
        "--pcie-phy-gen", type=int, choices=(1, 2, 3, 4), default=4
    )
    parser.add_argument(
        "--pcie-lane-mask", type=lambda v: int(v, 0), default=15
    )
    parser.add_argument("--pcie-phy-feedback", default="100ns")
    parser.add_argument("--pcie-training-fail", default="")
    parser.add_argument(
        "--pcie-fault",
        choices=(
            "tlp_crc",
            "dllp_crc",
            "drop_tlp",
            "drop_ack",
            "drop_update",
            "framing",
            "duplicate",
        ),
        default="",
    )
    parser.add_argument(
        "--pcie-fault-side", type=int, choices=(0, 1), default=0
    )
    parser.add_argument("--pcie-fault-count", type=int, default=1)
    args = parser.parse_args(namespace=Args())
    if args.pcie_cpl < args.pcie_rcb:
        parser.error("通用大读请求要求完成载荷上限不小于 RCB")
    if not 0 <= args.pcie_lane_mask <= 15 or args.pcie_fault_count < 0:
        parser.error("PCIe PHY lane 位图或有限故障次数无效")

    if args.workload == "baremetal" and not args.kernel:
        parser.error("--workload baremetal 需要 --kernel")
    if args.wait_gdb and args.workload != "baremetal":
        parser.error("--wait-gdb 只用于 --workload baremetal")

    board_cls = BOARDS[args.board]
    device = DEVICES[args.device]
    if any(
        v < 0 or v > 0xFFFF
        for v in (
            args.dram_fail_mask,
            args.dram_timeout_mask,
            args.dram_corrupt_mask,
        )
    ):
        parser.error("DRAM 反馈掩码需要位于 0..0xffff")
    if not device.precise_memory and (
        args.dram_fail_mask
        or args.dram_timeout_mask
        or args.dram_corrupt_selftest
        or args.dram_corrupt_mask
    ):
        parser.error("DRAM 反馈注入需要 gw100 timing 配置")
    if device.precise_memory and args.host == "atomic":
        parser.error(
            "LPDDR5 timing 配置支持 timing/o3，atomic 使用 gw100_compat"
        )
    missing = device.requires - board_cls.features()
    if missing:
        parser.error(
            f"设备 {args.device} 需要板卡提供 {sorted(missing)}，"
            f"板卡 {args.board} 不提供"
        )

    dev = device.make()
    if device.precise_memory:
        dev.link_speed = args.pcie_gen
        dev.link_width = args.pcie_width
        dev.pcie_rcb = args.pcie_rcb
        dev.pcie_completion_payload = args.pcie_cpl
        dev.pcie_rc_aspm = args.pcie_rc_aspm
        dev.pcie_host_service = args.pcie_host_service
        dev.pcie_phy_generation = args.pcie_phy_gen
        dev.pcie_phy_lane_mask = args.pcie_lane_mask
        dev.pcie_phy_feedback = args.pcie_phy_feedback
        dev.pcie_training_fail = args.pcie_training_fail
        dev.pcie_fault = args.pcie_fault
        dev.pcie_fault_side = args.pcie_fault_side
        dev.pcie_fault_count = args.pcie_fault_count
        if args.pcie_trace:
            dev.pcie_trace_path = str(
                Path(m5.options.outdir) / "pcie-link.jsonl"
            )
    elif args.pcie_fault or args.pcie_rc_aspm or args.pcie_trace:
        parser.error("PCIe 数字链路能力需要 gw100 timing/o3 配置")
    board = board_cls(
        HOSTS[args.host],
        [dev],
        bare_metal=(args.workload == "baremetal"),
    )
    if not os.path.isfile(args.rom):
        parser.error(f"启动 ROM 不存在 {args.rom}，请先构建固件")
    mgmt = device.make_mgmt(dev, args.rom)
    systems = {"gw100_mgmt": mgmt}
    memory = None
    if device.precise_memory:
        memory = make_memory_system(
            dev,
            mgmt,
            fail_mask=args.dram_fail_mask,
            timeout_mask=args.dram_timeout_mask,
            fault_phase=2 if args.dram_fault_phase == "calibrate" else 3,
            corrupt_selftest=args.dram_corrupt_selftest,
            corrupt_mask=args.dram_corrupt_mask,
        )
        systems["gw100_memory"] = memory
    board.attach_auxiliary_systems(systems)
    if args.workload == "baremetal":
        assert args.kernel, "前面已用 parser.error() 检查过 --kernel"
        board.set_baremetal_workload(
            args.kernel, wait_gdb=args.wait_gdb, gdb_port=args.gdb_port
        )
        # semihosting 的 SYS_EXIT 不是 stdlib Simulator
        # 认识的退出事件，裸机模式直接用底层
        # m5.instantiate()/m5.simulate()。
        board._pre_instantiate()
        # 管理 Tile 所在的 RISC-V 系统，与 host 系统在同一个 Root 下
        repo = Path(__file__).resolve().parents[2]
        sources = list((repo / "gem5-ext/src/gw100/pcie").rglob("*.cc"))
        sources += list((repo / "gem5-ext/src/gw100/pcie").rglob("*.hh"))
        sources += [
            repo / "gem5/src/sim/serialize.hh",
            repo / "spec-src/pcie/gw100-link.json",
            Path(args.kernel),
            Path(__file__),
        ]
        executable = Path(sys.executable)
        if executable.is_file():
            sources.append(executable)
        fingerprints = {
            str(path): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sources
        }
        m5.instantiate()
        board._post_instantiate()
        limit = args.max_ticks or m5.MaxTick
        event = m5.simulate(limit)
        while event.getCause() == "semi:ADP_Stopped_UserInterruption":
            if args.hardware_reset_test:
                mgmt.tile.externalReset(event.getCode())
            elif event.getCode() not in (0x100, 0x101):
                break
            event = m5.simulate(max(0, limit - m5.curTick()))
        cause = event.getCause()
        pcie_report = json.loads(dev.pcieReport())
        pcie_report["fingerprints"] = fingerprints
        pcie_report["inputs_unchanged_during_run"] = all(
            hashlib.sha256(path.read_bytes()).hexdigest()
            == fingerprints[str(path)]
            for path in sources
        )
        manifest = executable.with_suffix(".pcie-build.json")
        if manifest.is_file():
            built = json.loads(manifest.read_text())
            pcie_report["build"] = built
            pcie_report["build_matches_inputs"] = built[
                "executable_sha256"
            ] == fingerprints[str(executable)] and all(
                fingerprints.get(str(repo / name)) == value
                for name, value in built["sources"].items()
            )
        pcie_report["capture_tick"] = m5.curTick()
        pcie_report["host"] = args.host
        pcie_report["fault"] = args.pcie_fault
        pcie_report["fault_side"] = args.pcie_fault_side
        pcie_report["fault_count"] = args.pcie_fault_count
        pcie_report["trace"] = bool(args.pcie_trace)
        (Path(m5.options.outdir) / "pcie-report.json").write_text(
            json.dumps(pcie_report, indent=2, ensure_ascii=False) + "\n"
        )
        if memory is not None:
            controller = memory.controllers[0]
            report = {
                "profile": "generic-lpddr5-8433",
                "capture_tick": m5.curTick(),
                "channels": d.CHANNELS,
                "bytes": d.TOTAL_BYTES,
                "ck_ps": controller.dram.tCK.getValue(),
                "model_version": "gw100-memory-v0.2",
                "nominal_MT_s": d.DATA_RATE_MT_S,
                "ck_equivalent_MT_s": 8_000_000
                / controller.dram.tCK.getValue(),
                "profile_parameters": {
                    "read_slots": int(controller.dram.read_buffer_size),
                    "write_slots": int(controller.dram.write_buffer_size),
                    "page_policy": str(controller.dram.page_policy),
                    "scheduler": str(controller.mem_sched_policy),
                    "controller_front_ps": controller.static_frontend_latency.getValue(),
                    "controller_back_ps": controller.static_backend_latency.getValue(),
                    "calibration_ps": d.CALIBRATION_PS,
                    "training_ps": d.TRAIN_PS,
                    "adapter_bytes": d.FIFO_BYTES,
                    "adapter_clock_ps": d.CORE_PS,
                },
                "dram_timing_ps": {
                    key: getattr(controller.dram, key).getValue()
                    for key in (
                        "tCK",
                        "tCL",
                        "tCWL",
                        "tBURST",
                        "tRFC",
                        "tREFI",
                        "tXS",
                    )
                },
                "ingress_metric_names": [
                    "requests",
                    "read_bytes",
                    "write_bytes",
                    "retries",
                    "cdc_beat_ticks",
                    "queue_ticks",
                    "transfer_beat_ticks",
                ],
                "ingress_metrics": [
                    [i.metric(k) for k in range(7)] for i in memory.ingresses
                ],
                "tile_state": [
                    [
                        t.readRegister(off)
                        for off in (8, 16, 24, 0x1008, 0x2008)
                    ]
                    for t in memory.tiles
                ],
            }
            (Path(m5.options.outdir) / "memory.json").write_text(
                json.dumps(report, indent=2) + "\n"
            )
        if cause == APP_EXIT:
            code = event.getCode()
        elif args.max_ticks and m5.curTick() >= args.max_ticks:
            code = EXIT_TICK_LIMIT
        else:
            code = EXIT_OTHER
        print(f"bare-metal exit: {cause} (code {code})")
        sys.exit(code)

    set_linux_workload(board)
    Simulator(board=board).run()


if __name__ == "__m5_main__":
    main()
