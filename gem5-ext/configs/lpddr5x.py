"""LPDDR5X-8533 的 DRAM 时序（近似，待按 JEDEC LPDDR5X 规范逐项核对）。

从 gem5 自带的 LPDDR5_6400_1x16_BG_BL32 派生，只改随时钟变化的参数：
- tCK 以及以 CK 计的参数（突发传输时间、tCCD_L、总线切换等）按 6400/8533 缩短。
  这是物理规律：数据率提高，同样长度的突发传输得更快。
- 以纳秒计的阵列时序（tRCD、tRP、tRAS、tRFC 等）保持不变。
- 读延迟 tCL 也保持 LPDDR5-6400 的纳秒值，不随频率缩短。JEDEC 的 RL 以 CK 计，
  频率提高时 CK 数随之增加，纳秒值大致不变；按比例缩短会让 DRAM 延迟凭空变小。
  确切取值待 JEDEC 核对。
"""

from m5.objects import LPDDR5_6400_1x16_BG_BL32

_SCALE = 6400 / 8533


def _ns(v):
    return f"{v * _SCALE:.4f}ns"


class LPDDR5X_8533_1x16_BG_BL32(LPDDR5_6400_1x16_BG_BL32):
    tCK = _ns(1.25)
    tBURST = _ns(7.5)
    tBURST_MIN = _ns(2.5)
    tBURST_MAX = _ns(10)
    tCCD_L = _ns(10)
    tRTW = _ns(1.25)
    tCS = _ns(2.5)
    tPPD = _ns(2.5)
    tAAD = _ns(10)
    # tCL 沿用父类的 21.25ns（TBD：JEDEC 核对）
