/**
 * @file  dma_engine.cc
 * @brief DmaEngine 的实现
 *
 * @see SPEC-250, SPEC-320
 */

#include "src/compute_tile/dma/dma_engine.hh"

#include "base/logging.hh"

namespace gem5
{
namespace acis
{

DmaEngine::DmaEngine(const Params &p)
    : FunctionalUnit(p),
      localPort(name() + ".local", *this),
      remotePort(name() + ".remote", *this),
      reader(name() + ".reader", requestorId, p.req_bytes, p.max_outstanding),
      writer(name() + ".writer", requestorId, p.req_bytes, p.max_outstanding),
      localRange(p.local_range), dmaStats(*this)
{
}

Port &
DmaEngine::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "local")
        return localPort;
    if (if_name == "remote")
        return remotePort;
    return FunctionalUnit::getPort(if_name, idx);
}

UnitPort *
DmaEngine::selectPort(Addr addr)
{
    return localRange.contains(addr) ? &localPort : &remotePort;
}

void
DmaEngine::execute(const TensorInst &inst)
{
    panic_if(inst.opcode != isa::Opcode::DMA_COPY, "%s: v0 DMA 不支持 %s", name(),
             opcodeName(inst.opcode));
    panic_if(inst.dtype[0] != inst.dtype[2], "%s: DMA_COPY 不做类型转换", name());
    panic_if(inst.opnd[0].numElems() != inst.opnd[2].numElems(),
             "%s: DMA_COPY 源和目的元素个数不同", name());

    auto src = byteRuns(inst.opnd[0], inst.dtype[0]);
    auto dst = byteRuns(inst.opnd[2], inst.dtype[2]);
    uint64_t total = 0;
    for (const auto &r : src)
        total += r.bytes;
    buf.assign(total, 0);
    readyBytes = 0;
    dmaStats.bytes += total;

    auto sel = [this](Addr a) { return selectPort(a); };
    // 先启动写方（受 readyBytes 限制，此时发不出请求），再启动读方
    writer.startWrite(std::move(dst), buf.data(), sel, [this]() { finish(); }, &readyBytes);
    reader.startRead(std::move(src), buf.data(), sel,
                     [this]() {
                         readyBytes = reader.prefix();
                         writer.kick();
                     },
                     nullptr);
}

DmaEngine::DmaStats::DmaStats(DmaEngine &d)
    : statistics::Group(&d, "dma"),
      ADD_STAT(bytes, statistics::units::Byte::get(), "搬运的字节数")
{
}

} // namespace acis
} // namespace gem5
