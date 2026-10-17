/**
 * @file  dispatcher.hh
 * @brief 管理 Tile 的派发器：寄存器、doorbell 与集中派发
 *
 * v0 实现 SPEC-330 / SPEC-400 的最小子集。
 *
 * 流程：Host 写 CMD_ADDR 和 DOORBELL -> 用 DMA 读命令记录 -> 读程序 -> 按程序顺序把
 * 指令经指令通道下发给 tile_mask 指定的 Compute Tile -> 所有收到 END 的 Tile 报告完成后，
 * 把完成记录写回 Host 内存，STATUS 寄存器置为完成。
 *
 * 指令通道（arch_params: mgmt_tile.inst_channel）：每条指令占用通道
 * ceil(128 / bytes_per_cycle) 个周期，广播给多个 Tile 只占用一次，到达还要再加 latency。
 * 目标 Tile 的队列没有空位时，派发暂停，等任一 Tile 退休指令后再继续。
 *
 * @see SPEC-330, SPEC-400
 */

#ifndef __ACIS_MGMT_TILE_DISPATCHER_HH__
#define __ACIS_MGMT_TILE_DISPATCHER_HH__

#include <string>
#include <vector>

#include "base/statistics.hh"
#include "dev/dma_device.hh"
#include "params/AcisDispatcher.hh"
#include "sim/eventq.hh"
#include "src/common/fc.hh"

namespace gem5
{
namespace acis
{

class TileController;

class Dispatcher : public DmaDevice
{
  public:
    PARAMS(AcisDispatcher);
    Dispatcher(const Params &p);

    void init() override;

    AddrRangeList getAddrRanges() const override;
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

    /** 某个 Tile 的队列出现空位。 */
    void creditAvailable();
    /** 某个 Tile 执行完 END。 */
    void tileDone(unsigned tile);

  private:
    enum Status : uint32_t { Idle = 0, Busy = 1, Done = 2, Error = 3 };

    struct Entry
    {
        uint64_t mask;
        TensorInst inst;
    };

    void start();
    void commandFetched();
    void programFetched();
    void dispatch();
    void checkComplete();
    void complete(Status st, const std::string &why);
    void completionWritten();

    const Addr pioAddr;
    const Tick pioDelay;
    std::vector<TileController *> ctrls;
    const Cycles instLatency;
    const unsigned instBytesPerCycle;

    Status status = Idle;
    Status pendingStatus = Idle;
    uint64_t cmdAddr = 0;
    uint8_t cmdBuf[32] = {};
    uint8_t cplBuf[32] = {};
    std::vector<uint8_t> progBuf;

    uint64_t programAddr = 0;
    uint32_t programBytes = 0;
    uint64_t completionAddr = 0;
    uint64_t tag = 0;

    std::vector<Entry> entries;
    size_t next = 0;
    Tick channelFree = 0;
    bool waitingCredit = false;
    uint64_t endMask = 0;
    uint64_t doneMask = 0;
    Tick startTick = 0;
    Tick endTick = 0;

    EventFunctionWrapper cmdEvent;
    EventFunctionWrapper progEvent;
    EventFunctionWrapper cplEvent;
    EventFunctionWrapper dispatchEvent;

    struct DispStats : public statistics::Group
    {
        DispStats(Dispatcher &d);
        statistics::Scalar commands;
        statistics::Scalar instsDispatched;
        statistics::Scalar creditStalls;
        statistics::Scalar runTicks;
    } dispStats;
};

} // namespace acis
} // namespace gem5

#endif // __ACIS_MGMT_TILE_DISPATCHER_HH__
