"""
@file  __init__.py
@brief 被测设备的预设

每个预设是一组设备参数，提供 `make()` 创建 SimObject，`requires`
列出需要板卡提供的能力。预设与板卡、host 无关，由入口脚本组合。
加新的设备变体（例如 DSE 用的不同 SRAM、Tile 数）时只在这里登记。

- gw100：当前的 GW100 PCIe endpoint

@see spec-pcie, spec-hdi
"""

from .gw100 import GW100

DEVICES = {
    "gw100": GW100,
}

DEFAULT_DEVICE = "gw100"
