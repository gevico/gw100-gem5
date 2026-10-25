"""
@file  params.py
@brief GW100 ARM host 的第一步参数

先用最小常量让板卡跑起来；地址映射、BAR 窗口和 ABI
常量等单一真源重建后再从这里接入。不要把驱动/测试用的
地址硬编码到入口脚本里。

@see SPEC-110, SPEC-310, SPEC-400
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Gw100Params:
    """第一步的 host/device 参数。"""

    clk_freq: str = "3GHz"
    mem_size: str = "2GiB"
    num_cores: int = 2
    bar0_size: str = "4KiB"
    vendor_id: int = 0x1234
    device_id: int = 0x1000


def load_params() -> Gw100Params:
    """返回默认参数；单一真源重建后改为从 YAML 读取。"""
    return Gw100Params()
