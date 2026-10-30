"""
@file  run_gw100.py
@brief GW100 仿真的入口脚本：组合板卡、host 和设备

用法（在 gem5 目录执行）：
    ./build/ARM/gem5.opt ../gem5-ext/configs/run_gw100.py \
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

@see man-gem5-user, spec-pcie
"""

import argparse
import sys

import m5
from gem5.simulate.simulator import Simulator

from gw100.boards import BOARDS, DEFAULT_BOARD
from gw100.devices import DEFAULT_DEVICE, DEVICES
from gw100.hosts import DEFAULT_HOST, HOSTS
from gw100.workload import set_linux_workload

APP_EXIT = "semi:ADP_Stopped_ApplicationExit"
EXIT_TICK_LIMIT = 124
EXIT_OTHER = 125


def main() -> None:
    parser = argparse.ArgumentParser(description="GW100 board/host/device")
    parser.add_argument(
        "--board",
        choices=sorted(BOARDS),
        default=DEFAULT_BOARD,
        help=f"板卡，见 gw100/boards/（默认 {DEFAULT_BOARD}）",
    )
    parser.add_argument(
        "--host",
        choices=sorted(HOSTS),
        default=DEFAULT_HOST,
        help=f"host 预设，见 gw100/hosts/（默认 {DEFAULT_HOST}）",
    )
    parser.add_argument(
        "--device",
        choices=sorted(DEVICES),
        default=DEFAULT_DEVICE,
        help=f"设备预设，见 gw100/devices/（默认 {DEFAULT_DEVICE}）",
    )
    parser.add_argument(
        "--workload",
        choices=["baremetal", "linux"],
        default="baremetal",
        help="默认 baremetal；linux 仅用于对比",
    )
    parser.add_argument(
        "--kernel",
        help="bare-metal ELF 路径（--workload baremetal 时必填）",
    )
    parser.add_argument(
        "--max-ticks",
        type=int,
        default=0,
        help="裸机仿真的 tick 上限，0 表示不限（默认）；用于防止程序死循环",
    )
    args = parser.parse_args()

    if args.workload == "baremetal" and not args.kernel:
        parser.error("--workload baremetal 需要 --kernel")

    board_cls = BOARDS[args.board]
    device = DEVICES[args.device]
    missing = device.requires - board_cls.features()
    if missing:
        parser.error(
            f"设备 {args.device} 需要板卡提供 {sorted(missing)}，"
            f"板卡 {args.board} 不提供"
        )

    board = board_cls(
        HOSTS[args.host],
        [device.make()],
        bare_metal=(args.workload == "baremetal"),
    )
    if args.workload == "baremetal":
        board.set_baremetal_workload(args.kernel)
        # semihosting 的 SYS_EXIT 不是 stdlib Simulator
        # 认识的退出事件，裸机模式直接用底层
        # m5.instantiate()/m5.simulate()。
        board._pre_instantiate()
        m5.instantiate()
        board._post_instantiate()
        event = m5.simulate(args.max_ticks or m5.MaxTick)
        cause = event.getCause()
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
