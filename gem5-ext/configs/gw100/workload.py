"""
@file  workload.py
@brief GW100 ARM host 的工作负载选择

Linux 走 `set_linux_workload`；bare-metal 只提供 workload 类，由
`boards.arm_vexpress.ArmVExpressBoard.set_baremetal_workload()` 触发板卡初始化。

@see MAN-050
"""

from m5.objects import ArmFsWorkload

from gem5.resources.resource import obtain_resource


class ArmBaremetal(ArmFsWorkload):
    """把裸机 ELF 作为 kernel 加载，不使用 DTB/bootloader。"""

    dtb_addr = 0

    def __init__(self, obj, system, **kwargs):
        super().__init__(**kwargs)
        self.object_file = obj


def set_linux_workload(board) -> None:
    """启动 ARM Ubuntu 24.04；资源首次运行时会自动下载。"""
    board.set_workload(
        obtain_resource(
            "arm-ubuntu-24.04-boot-with-systemd",
            resource_version="3.0.0",
        )
    )
