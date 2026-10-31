/**
 * @file  devmem_backend.cc
 * @brief DevmemBackend 的实现
 *
 * @see spec-pcie, spec-address-map
 */

#include "src/gw100/pcie/bar2/devmem_backend.hh"

#include <memory>

#include "mem/packet.hh"

namespace gem5
{
namespace gw100
{

/**
 * 读或写 DPA 起的 size 字节：写时把 data 写入设备内存，读时把读到
 * 的数据放入 data。本阶段为功能访问，返回 0，延迟由调用者另计
 */
Tick
DevmemBackend::access(Addr dpa, unsigned size, bool write, uint8_t *data)
{
    RequestPtr req = std::make_shared<Request>(dpa, size, 0, requestorId);
    Packet pkt(req, write ? MemCmd::WriteReq : MemCmd::ReadReq);
    pkt.dataStatic(data);
    memPort.sendFunctional(&pkt);
    return 0;
}

} // namespace gw100
} // namespace gem5
