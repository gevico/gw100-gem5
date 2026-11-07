"""
@file  LPDDR5.py
@brief 通用 LPDDR5 高速工作点的架构性能参数

沿用上游 LPDDR5-5500 的组织和纳秒级阵列时序，按目标传输率推算
CK、分段 burst 及命令总线间隔。参数计算和建模假设见规格附录。

@see spec-dram
"""

from m5.objects.DRAMInterface import LPDDR5_5500_1x16_BG_BL32


# 8433 MT/s、WCK:CK=4:1，每个 CK 对应八拍，单位 ns（推算）。
_TCK_NS = 8000.0 / 8433


def _cycles(count):
    """将 CK 周期数转换为供 gem5 量化的纳秒参数。"""
    return f"{count * _TCK_NS:.12f}ns"


class LPDDR5_8433_1x16_BG_BL32(LPDDR5_5500_1x16_BG_BL32):
    """8433 MT/s、单 x16、BG、BL32 的通用性能预设。"""

    # CK、WCK 和数据传输比例与基类相同。
    tCK = _cycles(1)

    # 两段 BL16 各占两 CK，中间间隔两 CK。
    tBURST = _cycles(6)
    tBURST_MIN = _cycles(2)
    tBURST_MAX = _cycles(8)

    # 同 bank group 的列访问间隔。
    tCCD_L = _cycles(8)

    # 沿用基类 RL=WL 的调度假设和额外一个 CK 的读转写间隔。
    tRTW = _cycles(1)

    # rank 切换、连续 PRE，以及双阶段 ACT 的最大间隔。
    tCS = _cycles(2)
    tPPD = _cycles(2)
    tAAD = _cycles(8)

    # 基类的读到 PRE 约束为 max(7.5 ns, 2 CK) - 2 CK。
    tRTP = f"{max(7.5, 2 * _TCK_NS) - 2 * _TCK_NS:.12f}ns"
