/**
 * @file  dma_engine.hh
 * @brief Compute Tile 的 DMA 引擎
 *
 * v0 实现 DMA_COPY。
 *
 * 边读边写：读回的数据只要从头连续到齐一段，就立即写出这一段。
 * 访问本 Tile SRAM 走 local 端口，其他地址（DRAM、其他 Tile 的 SRAM）走 remote 端口。
 *
 * @see SPEC-250, SPEC-320
 */

#ifndef __ACIS_COMPUTE_TILE_DMA_DMA_ENGINE_HH__
#define __ACIS_COMPUTE_TILE_DMA_DMA_ENGINE_HH__

#include <vector>

#include "base/statistics.hh"
#include "params/AcisDmaEngine.hh"
#include "src/common/mem_streamer.hh"
#include "src/compute_tile/functional_unit.hh"

namespace gem5
{
namespace acis
{

class DmaEngine : public FunctionalUnit
{
  public:
    PARAMS(AcisDmaEngine);
    DmaEngine(const Params &p);

    Port &getPort(const std::string &if_name, PortID idx = InvalidPortID) override;

  protected:
    void execute(const TensorInst &inst) override;

  private:
    UnitPort *selectPort(Addr addr);

    UnitPort localPort;
    UnitPort remotePort;
    MemStreamer reader;
    MemStreamer writer;
    const AddrRange localRange;

    std::vector<uint8_t> buf;
    uint64_t readyBytes = 0;

    struct DmaStats : public statistics::Group
    {
        DmaStats(DmaEngine &d);
        statistics::Scalar bytes;
    } dmaStats;
};

} // namespace acis
} // namespace gem5

#endif // __ACIS_COMPUTE_TILE_DMA_DMA_ENGINE_HH__
