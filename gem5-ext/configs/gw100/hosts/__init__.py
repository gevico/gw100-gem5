"""
@file  __init__.py
@brief host 的预设

板卡（boards/）决定平台结构：用哪个平台、PCIe 怎么接、设备怎么挂；
host 预设只决定 CPU 模型、核数、频率、内存和 cache 这些参数。
预设与板卡、设备无关，由入口脚本组合；加新的 host 时只在这里加一项。

- timing：默认。TIMING CPU，日常开发和设备功能验证用，仿真快
- o3：O3 CPU，测 host 侧软件开销、暴露驱动的内存序问题时用
- atomic：ATOMIC CPU，功能模式。访问立即完成、不计排队和竞争，只用来
  验证功能正确，不产出性能数字

两个预设都还没有对标真实的端侧主机，host 侧的时序不能当结论。

@see man-gem5-user
"""

from dataclasses import dataclass, replace

from gem5.components.processors.cpu_types import CPUTypes


@dataclass(frozen=True)
class HostConfig:
    """host 侧的一组参数；内存类型目前固定为 DDR4-2400 双通道。"""

    cpu_type: CPUTypes
    num_cores: int
    clk_freq: str
    mem_size: str
    l1i_size: str
    l1d_size: str
    l2_size: str


TIMING = HostConfig(
    cpu_type=CPUTypes.TIMING,
    num_cores=2,
    clk_freq="3GHz",
    mem_size="2GiB",
    l1i_size="64KiB",
    l1d_size="64KiB",
    l2_size="1MiB",
)

O3 = replace(TIMING, cpu_type=CPUTypes.O3)

ATOMIC = replace(TIMING, cpu_type=CPUTypes.ATOMIC)

HOSTS = {
    "timing": TIMING,
    "o3": O3,
    "atomic": ATOMIC,
}

DEFAULT_HOST = "timing"
