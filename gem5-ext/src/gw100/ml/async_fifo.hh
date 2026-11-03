/**
 * @file  async_fifo.hh
 * @brief ML 通道：两端时钟不同、深度有限的异步 FIFO
 *
 * 发送方在自己的时钟沿写入一拍；写指针经接收方的同步器（sync_stages
 * 级）后，接收方才能看到这一拍。接收方读出一拍后，读指针同样经发送方
 * 的同步器返回，发送方才能重新使用这个空位。深度即信用数
 * （spec-mgmt-link §6.1）。
 *
 * @see spec-mgmt-link
 */

#ifndef __GW100_ML_ASYNC_FIFO_HH__
#define __GW100_ML_ASYNC_FIFO_HH__

#include <algorithm>
#include <cassert>
#include <deque>

#include "src/gw100/ml/ml_types.hh"

namespace gem5
{
namespace gw100
{

/** 深度有限的异步 FIFO；P 是每拍携带的负载（事务或响应的指针） */
template <class P>
class AsyncFifo
{
  public:
    /** FIFO 中的一拍 */
    struct Beat
    {
        /** 这一拍所属的事务或响应 */
        P payload;
        /** 拍号，从 0 开始 */
        unsigned idx;
        /** 是否为该事务的最后一拍 */
        bool last;
        /** 接收方最早能看到它的时刻 */
        Tick visibleAt;
    };

    /** 构造：depth 为深度（拍），syncStages 为同步器级数 */
    AsyncFifo(unsigned depth, unsigned sync_stages)
        : depth(depth), syncStages(sync_stages)
    {
        assert(depth > 0 && sync_stages > 0);
    }

    /** 绑定发送方和接收方的时钟 */
    void
    bind(MlClockIf *send_clk, MlClockIf *recv_clk)
    {
        sender = send_clk;
        receiver = recv_clk;
    }

    /** 发送方在时刻 t 是否有空位 */
    bool
    canPush(Tick t)
    {
        prune(t);
        return beats.size() + returning.size() < depth;
    }

    /**
     * 发送方在 t 时没有空位时，最早出现空位的时刻；接收方还没读出
     * 任何一拍时返回 MaxTick
     */
    Tick
    nextFreeAt(Tick t)
    {
        prune(t);
        if (beats.size() + returning.size() < depth)
            return t;
        return returning.empty() ? MaxTick : returning.front();
    }

    /** 发送方在时刻 t 写入一拍；调用前须 canPush(t) */
    void
    push(P payload, unsigned idx, bool last, Tick t)
    {
        assert(canPush(t));
        Tick vis = receiver->edgeAfter(t) +
                   (syncStages - 1) * receiver->period();
        beats.push_back(Beat{payload, idx, last, vis});
        receiver->wakeAt(vis);
    }

    /** FIFO 是否为空 */
    bool empty() const { return beats.empty(); }

    /** 队首这一拍（调用前须非空） */
    const Beat &front() const { return beats.front(); }

    /** 队首这一拍在时刻 t 是否已对接收方可见 */
    bool
    frontVisible(Tick t) const
    {
        return !beats.empty() && beats.front().visibleAt <= t;
    }

    /** 接收方在时刻 t 读出队首一拍；空位经同步器返回发送方 */
    void
    pop(Tick t)
    {
        assert(frontVisible(t));
        beats.pop_front();
        Tick back = sender->edgeAfter(t) +
                    (syncStages - 1) * sender->period();
        returning.push_back(back);
        sender->wakeAt(back);
    }

    /** 复位：两端的指针清零，FIFO 变空 */
    void
    reset()
    {
        beats.clear();
        returning.clear();
    }

    /** 当前占用的拍数（含正在返回的空位），供测试和统计 */
    unsigned
    occupancy(Tick t)
    {
        prune(t);
        return beats.size() + returning.size();
    }

    /** 深度（拍） */
    const unsigned depth;

  private:
    /** 丢掉在 t 之前已经返回发送方的空位 */
    void
    prune(Tick t)
    {
        while (!returning.empty() && returning.front() <= t)
            returning.pop_front();
    }

    /** 同步器级数 */
    const unsigned syncStages;
    /** 发送方的时钟 */
    MlClockIf *sender = nullptr;
    /** 接收方的时钟 */
    MlClockIf *receiver = nullptr;
    /** FIFO 中尚未被读出的拍 */
    std::deque<Beat> beats;
    /** 已被读出、尚未返回发送方的空位，按返回时刻排序 */
    std::deque<Tick> returning;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_ML_ASYNC_FIFO_HH__
