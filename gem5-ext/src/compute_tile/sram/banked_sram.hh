/**
 * @file  banked_sram.hh
 * @brief Compute Tile 内多端口、分 bank 的共享 SRAM
 *
 * 时序模型：
 * - 地址按 bankBytes 交织到 numBanks 个 bank；一个请求占用它覆盖的每个 bank 一个周期；
 * - 每个端口每周期接收一个请求；
 * - 请求的开始时刻 = max(到达时刻, 端口空闲时刻, 所覆盖 bank 的空闲时刻)，
 *   响应在开始后 latency 个周期返回。bank 冲突表现为开始时刻被推迟。
 * 数据存储和后门（功能）访问由 AbstractMemory 提供。
 *
 * @see SPEC-200
 */

#ifndef __GW100_COMPUTE_TILE_SRAM_BANKED_SRAM_HH__
#define __GW100_COMPUTE_TILE_SRAM_BANKED_SRAM_HH__

#include <memory>
#include <vector>

#include "base/statistics.hh"
#include "mem/abstract_mem.hh"
#include "mem/qport.hh"
#include "params/Gw100BankedSram.hh"

namespace gem5
{
namespace gw100
{

class BankedSram : public memory::AbstractMemory
{
  private:
    class SramPort : public QueuedResponsePort
    {
      public:
        SramPort(const std::string &name, BankedSram &mem, PortID id);

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        AddrRangeList getAddrRanges() const override;

      private:
        RespPacketQueue queue;
        BankedSram &mem;
    };

  public:
    PARAMS(Gw100BankedSram);
    BankedSram(const Params &p);

    void init() override;
    Port &getPort(const std::string &if_name, PortID idx = InvalidPortID) override;

  private:
    bool handleTiming(PacketPtr pkt, SramPort &port, PortID idx);

    std::vector<std::unique_ptr<SramPort>> ports;
    const unsigned numBanks;
    const unsigned bankBytes;
    const Cycles latency;
    std::vector<Tick> bankFree;
    std::vector<Tick> portFree;

    struct SramStats : public statistics::Group
    {
        SramStats(BankedSram &mem);
        statistics::Scalar requests;
        statistics::Scalar bytes;
        statistics::Scalar conflictCycles;
    } sramStats;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_COMPUTE_TILE_SRAM_BANKED_SRAM_HH__
