"""
@file  run_gw100.py
@brief GW100 ARM host 的入口脚本

用法（在 gem5 目录执行）：
    ./build/ARM/gem5.opt ../gem5-ext/configs/run_gw100.py \
        --workload baremetal \
        --kernel /path/to/hello.elf

默认是 bare-metal，不启动 Linux、不生成 DTB，也不使用
bootloader。

@see MAN-050, SPEC-310
"""

import argparse

import m5
from gem5.simulate.simulator import Simulator

from gw100.board_arm import Gw100ArmBoard
from gw100.params import load_params
from gw100.workload import set_linux_workload


def main() -> None:
    parser = argparse.ArgumentParser(description="GW100 ARM host minimum boot")
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
    args = parser.parse_args()

    if args.workload == "baremetal" and not args.kernel:
        parser.error("--workload baremetal 需要 --kernel")

    board = Gw100ArmBoard(
        load_params(), bare_metal=(args.workload == "baremetal")
    )
    if args.workload == "baremetal":
        board.set_baremetal_workload(args.kernel)
        # semihosting 的 SYS_EXIT 不是 stdlib Simulator
        # 认识的退出事件，裸机模式直接用底层
        # m5.instantiate()/m5.simulate()。
        board._pre_instantiate()
        m5.instantiate()
        board._post_instantiate()
        event = m5.simulate()
        cause = event.getCause()
        print(f"bare-metal exit: {cause}")
        if cause != "semi:0x0":
            raise SystemExit(1)
        return

    set_linux_workload(board)
    Simulator(board=board).run()


if __name__ == "__m5_main__":
    main()
