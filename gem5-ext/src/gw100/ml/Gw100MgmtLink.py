"""
@file  Gw100MgmtLink.py
@brief 管理链路 ML 的 SimObject 声明

ML 连接 PCIe Tile 的 HIF 与管理 Tile，两端分属 host 系统和管理 Tile 的
系统。本对象持有两端适配器和四个通道；HIF 侧由 Gw100PciDevice 接入，
管理 Tile 侧由 Gw100MgmtTile 接入，各自用自己的时钟域驱动。

@see spec-mgmt-link
"""

from m5.params import *
from m5.SimObject import SimObject


class Gw100MgmtLink(SimObject):
    """管理链路 ML：两端适配器与四个异步通道。"""

    type = "Gw100MgmtLink"
    cxx_header = "src/gw100/ml/mgmt_link.hh"
    cxx_class = "gem5::gw100::Gw100MgmtLink"

    req_fifo_beats = Param.Unsigned(8, "请求通道的深度（ml.req_fifo_beats）")
    rsp_fifo_beats = Param.Unsigned(8, "响应通道的深度（ml.rsp_fifo_beats）")
    sync_stages = Param.Unsigned(2, "同步器级数")
    hif_tags = Param.Unsigned(32, "HIF 侧的 tag 数（ml.hif_tags）")
    mgmt_tags = Param.Unsigned(4, "管理 Tile 侧的 tag 数（ml.mgmt_tags）")
    timeout = Param.Latency("100us", "超时与通道停滞的判定时间（ml.timeout）")
