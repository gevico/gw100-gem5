"""
@file  __init__.py
@brief GW100 的 host 板卡

每个模块对应一种平台结构（平台、PCIe、中断、启动方式）。CPU、内存、
cache 等参数由 `gw100.hosts` 的预设提供；挂什么设备由 `gw100.devices`
的预设决定，板卡只提供插槽。板卡用 `features()` 声明自己提供的能力。

- arm_vexpress：AArch64，VExpress_GEM5_Foundation 平台

@see man-gem5-user, spec-pcie
"""

from .arm_vexpress import ArmVExpressBoard

BOARDS = {
    "arm_vexpress": ArmVExpressBoard,
}

DEFAULT_BOARD = "arm_vexpress"
