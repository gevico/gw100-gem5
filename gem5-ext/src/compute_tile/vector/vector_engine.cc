/**
 * @file  vector_engine.cc
 * @brief VectorEngine 的实现
 *
 * @see SPEC-220
 */

#include "src/compute_tile/vector/vector_engine.hh"

#include "base/intmath.hh"
#include "base/logging.hh"
#include "sim/eventq.hh"
#include "src/common/fc.hh"

namespace gem5
{
namespace gw100
{

VectorEngine::VectorEngine(const Params &p)
    : FunctionalUnit(p),
      memPort(name() + ".mem", *this),
      readA(name() + ".readA", requestorId, p.req_bytes, p.max_outstanding),
      readB(name() + ".readB", requestorId, p.req_bytes, p.max_outstanding),
      writeC(name() + ".writeC", requestorId, p.req_bytes, p.max_outstanding),
      lanes(p.lanes), pipelineLatency(p.pipeline_latency), localRange(p.local_range)
{
    fatal_if(lanes == 0, "%s: lanes 必须大于 0", name());
}

Port &
VectorEngine::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "mem")
        return memPort;
    return FunctionalUnit::getPort(if_name, idx);
}

void
VectorEngine::checkLocal(const TensorDesc &t, isa::DType dt, const char *what) const
{
    for (const auto &r : byteRuns(t, dt)) {
        panic_if(!localRange.contains(r.addr) || !localRange.contains(r.addr + r.bytes - 1),
                 "%s: %s 的地址 %#x 不在本 Tile SRAM 内；向量引擎只能访问本 Tile SRAM",
                 name(), what, r.addr);
    }
}

void
VectorEngine::execute(const TensorInst &inst)
{
    using isa::Opcode;
    panic_if(inst.opcode != Opcode::VEC_BINARY && inst.opcode != Opcode::VEC_SCALAR,
             "%s: v0 向量引擎不支持 %s", name(), opcodeName(inst.opcode));
    cur = inst;
    const uint64_t n = inst.opnd[2].numElems();
    panic_if(inst.opnd[0].numElems() != n, "%s: 操作数 0 与目的元素个数不同", name());

    checkLocal(inst.opnd[0], inst.dtype[0], "操作数 0");
    checkLocal(inst.opnd[2], inst.dtype[2], "目的操作数");
    bufA.assign(fc::denseBytes(inst.dtype[0], n), 0);
    bufC.assign(fc::denseBytes(inst.dtype[2], n), 0);

    auto sel = [this](Addr) { return &memPort; };
    pendingReads = 1;
    const bool binary = inst.opcode == Opcode::VEC_BINARY;
    if (binary) {
        panic_if(inst.opnd[1].numElems() != n, "%s: 操作数 1 与目的元素个数不同", name());
        checkLocal(inst.opnd[1], inst.dtype[1], "操作数 1");
        bufB.assign(fc::denseBytes(inst.dtype[1], n), 0);
        pendingReads = 2;
    }
    readA.startRead(byteRuns(inst.opnd[0], inst.dtype[0]), bufA.data(), sel, nullptr,
                    [this]() { operandArrived(); });
    if (binary) {
        readB.startRead(byteRuns(inst.opnd[1], inst.dtype[1]), bufB.data(), sel, nullptr,
                        [this]() { operandArrived(); });
    }
}

void
VectorEngine::operandArrived()
{
    if (--pendingReads > 0)
        return;
    const uint64_t n = cur.opnd[2].numElems();
    const Cycles cycles(divCeil(n, lanes) + pipelineLatency);
    schedule(new EventFunctionWrapper([this]() { compute(); }, name() + ".compute", true),
             clockEdge(cycles));
}

void
VectorEngine::compute()
{
    using isa::Opcode;
    const uint64_t n = cur.opnd[2].numElems();
    if (functional) {
        bool ok;
        if (cur.opcode == Opcode::VEC_BINARY) {
            ok = fc::vecBinary(static_cast<isa::VecBinaryOp>(cur.subop), cur.dtype[0],
                               bufA.data(), cur.dtype[1], bufB.data(), cur.dtype[2],
                               bufC.data(), n);
        } else {
            ok = fc::vecScalar(static_cast<isa::VecScalarOp>(cur.subop), cur.dtype[0],
                               bufA.data(), cur.imm, cur.dtype[2], bufC.data(), n);
        }
        panic_if(!ok, "%s: 不支持的 subop %u", name(), cur.subop);
    }
    writeC.startWrite(byteRuns(cur.opnd[2], cur.dtype[2]), bufC.data(),
                      [this](Addr) { return &memPort; }, [this]() { finish(); });
}

} // namespace gw100
} // namespace gem5
