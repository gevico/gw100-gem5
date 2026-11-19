/**
 * @file ct_nidma.hh
 * @brief compute tile 内部的 nidma：数据请求，noc 数据处理
 *
 * @see spec-ct
 */

#ifndef __GW100_CT_NIDMA_CT_NIDMA_HH__
#define __GW100_CT_NIDMA_CT_NIDMA_HH__

#include <functional>
#include <vector>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"

namespace gem5
{
namespace gw100
{
/** 实现外部 64b 和内部大块地址通信的集合，将外部 noc 通信和内部的数据搬运结合在一起 */
class CtNidma
{
    public: 
    /** 构造函数，实现初始化部分 */
    explicit CtNidma(CtClockIf &clock_if,CtNocIf &noc_if, CtWindowHandler &win_handler);
    /** 小于 64b 的数据接收,交付 windows */
    CtAccessResult recvExternalPacket(CtAccess &a,Tick now);
    /** dma 指令交付传递 */
    bool subDma(const CtNidmaDesc &desc,std::function<void(CtStatus)> done=nullptr);
    /** 检测 noc 是否空闲，并继续发送数据 */
    void step();
    void reset();
    /** 判断当前是否忙碌 */
    bool busy() const;

    private:
    bool isBusy = false;
    /** 64b 数据包的排队队列 */
    std::vector<uint8_t> buf;
    /** 任务结束后与 riscfive 控制核的回调函数 */
    std::function<void(CtStatus)> invoke;
    /** 记录数据交互情况的状态位 */
    /** 已经切包发出去的字节数 */
    unsigned recvBytes;
    /** 已经收到外部相应确认的字节数 */
    unsigned flightBytes;
    /** 正在 noc 网络上的字节数 */
    unsigned sendBytes;
    /** 某块 64b 包是否有错误 */
    bool hasError;
    /** 当前指令的副本 */
    CtNidmaDesc curDesc;
    /** 将大块数据切成 64b 包交由 noc 处理 */
    void packetSlice();
    /** 将 64b 包聚合成大块数据并交给 windows */
    bool FlushWindows();
    /** 大块数据读取 */
    bool loadWindows();
    CtClockIf &clock_if;
    CtNocIf &noc_if;
    CtWindowHandler &win_handler;
    //todo 添加在 noc 网络上的自己上限
};
}
}

#endif