/**
 * @file  devmem_backend.hh
 * @brief PCIe Tile 访问设备内存的后端
 *
 * BAR2 调试窗口经它访问设备内存。本阶段经 mem_port 做功能访问，延迟由
 * 调用者另计（理想化清单 #9）；以后改为经 PCIe Tile 的 NI 以 timing 方式
 * 访问 NoC，只需替换本类的实现，调用者不变。
 *
 * @see spec-pcie, spec-address-map
 */

#ifndef __GW100_PCIE_BAR2_DEVMEM_BACKEND_HH__
#define __GW100_PCIE_BAR2_DEVMEM_BACKEND_HH__

#include <cstdint>
#include <string>

#include "base/logging.hh"
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/request.hh"

namespace gem5
{
namespace gw100
{

/** 访问设备内存的后端：持有接设备内存的端口 */
class DevmemBackend
{
  public:
    /** 构造；port_name 是端口的完整名字，id 是访问设备内存的请求者 ID */
    DevmemBackend(const std::string &port_name, RequestorID id)
        : memPort(port_name), requestorId(id)
    {}

    /** 接设备内存的端口，供所属设备的 getPort() 返回 */
    Port &port() { return memPort; }

    /** 访问设备内存时使用的请求者 ID */
    RequestorID id() const { return requestorId; }

    /**
     * 读或写 DPA 起的 size 字节：写时把 data 写入设备内存，读时把读到
     * 的数据放入 data。本阶段为功能访问，返回 0，延迟由调用者另计
     */
    Tick access(Addr dpa, unsigned size, bool write, uint8_t *data);

  private:
    /** 接设备内存的端口；本阶段只做功能访问 */
    class MemPort : public RequestPort
    {
      public:
        /** 构造名为 name 的端口 */
        MemPort(const std::string &name) : RequestPort(name) {}

      protected:
        /** 本阶段不发 timing 请求，收到响应说明用法错误 */
        bool recvTimingResp(PacketPtr) override
        { panic("DevmemBackend::MemPort 只支持功能访问"); }

        /** 本阶段不发 timing 请求，收到重试说明用法错误 */
        void recvReqRetry() override
        { panic("DevmemBackend::MemPort 只支持功能访问"); }
    };

    /** 接设备内存的端口 */
    MemPort memPort;
    /** 访问设备内存时使用的请求者 ID */
    RequestorID requestorId;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR2_DEVMEM_BACKEND_HH__
