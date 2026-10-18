/**
 * @file  vector_engine.hh
 * @brief Compute Tile 的向量引擎
 *
 * v0 实现 VEC_BINARY、VEC_SCALAR。
 *
 * 时序：读操作数（并行）-> 计算 ceil(元素数 / lanes) + 流水线延迟 个周期 -> 写结果。
 * v0 三个阶段依次进行；按块流水重叠留待后续版本（见 MAN-060）。
 * 向量引擎只能访问本 Tile 的 SRAM。
 *
 * @see SPEC-220
 */

#ifndef __GW100_COMPUTE_TILE_VECTOR_VECTOR_ENGINE_HH__
#define __GW100_COMPUTE_TILE_VECTOR_VECTOR_ENGINE_HH__

#include <vector>

#include "params/Gw100VectorEngine.hh"
#include "src/common/mem_streamer.hh"
#include "src/compute_tile/functional_unit.hh"

namespace gem5
{
namespace gw100
{

class VectorEngine : public FunctionalUnit
{
  public:
    PARAMS(Gw100VectorEngine);
    VectorEngine(const Params &p);

    Port &getPort(const std::string &if_name, PortID idx = InvalidPortID) override;

  protected:
    void execute(const TensorInst &inst) override;

  private:
    void operandArrived();
    void compute();
    void checkLocal(const TensorDesc &t, isa::DType dt, const char *what) const;

    UnitPort memPort;
    MemStreamer readA;
    MemStreamer readB;
    MemStreamer writeC;
    const unsigned lanes;
    const Cycles pipelineLatency;
    const AddrRange localRange;

    TensorInst cur;
    std::vector<uint8_t> bufA, bufB, bufC;
    unsigned pendingReads = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_COMPUTE_TILE_VECTOR_VECTOR_ENGINE_HH__
