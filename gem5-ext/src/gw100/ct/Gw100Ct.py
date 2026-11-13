"""
@file  Gw100Ct.py
@brief Compute Tile 的 SimObject 声明

Compute Tile 通过 nidma 挂载在 noc 网络上并实现片上通信，管理 Tile 和
其他 Compute Tile 可以直接读写暴漏出的接口进行本地状态的修改

@see spec-ct
"""

from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *

class Gw100Ct(ClockedObject):
    """Compute Tile: 具体数据计算的终端处理侧"""
    
    type = "Gw100Ct"
    cxx_header = "src/gw100/ct/gw100_ct_device.hh"
    cxx_class = "gem5::gw100::Gw100Ct" 
    
    pio = ResponsePort("外部读写接口")
    
    # todo这里配置上需要注意一个普遍性多实现的问题，对应 tile 有对应的坐标
    window_base = Param.Addr(0x10_0100_0000, "...")
    
    
    
    
