/**
 * @file  devmem_backend.hh
 * @brief PCIe Tile 访问设备内存的后端
 *
 * BAR2 调试窗口经它做功能访问，延迟由调用者另计（理想化清单 #9）；
 * HDMA 经它的 HdmaDevIf 接口做 timing 访问，在途请求有上限，被反压时等
 * 重试。NoC 接入后改为经 PCIe Tile 的 NI 访问（理想化清单 #10），调用者
 * 不变。
 *
 * @see spec-pcie, spec-address-map
 */

#ifndef __GW100_PCIE_BAR2_DEVMEM_BACKEND_HH__
#define __GW100_PCIE_BAR2_DEVMEM_BACKEND_HH__

#include <cstdint>
#include <functional>
#include <string>

#include "base/logging.hh"
#include "base/types.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "mem/request.hh"
#include "src/gw100/pcie/hdma/hdma_if.hh"

namespace gem5
{
namespace gw100
{

/**
 * 访问设备内存的后端：持有接设备内存的端口。BAR2 的窗口访问是功能
 * 访问（理想化清单 #9）；HDMA 经 HdmaDevIf 发 timing 请求，在途请求数
 * 有上限，被反压时等重试
 */
class DevmemBackend : public HdmaDevIf
{
  public:
    /**
     * 构造；port_name 是端口的完整名字，id 是访问设备内存的请求者 ID，
     * max_outstanding 是 timing 请求的在途上限
     */
    DevmemBackend(const std::string &port_name, RequestorID id,
                  unsigned max_outstanding);

    /** 接设备内存的端口，供所属设备的 getPort() 返回 */
    Port &port() { return memPort; }

    /** 访问设备内存时使用的请求者 ID */
    RequestorID id() const { return requestorId; }

    /**
     * 读或写 DPA 起的 size 字节：写时把 data 写入设备内存，读时把读到
     * 的数据放入 data。功能访问，返回 0，延迟由调用者另计
     */
    Tick access(Addr dpa, unsigned size, bool write, uint8_t *data);

    /** 能否再接受一个 timing 请求 */
    bool canSend() const override;

    /** timing 读 DPA 起的 len 字节 */
    void read(Addr dpa, unsigned len, DevReadDone done) override;

    /** timing 写 DPA 起的 len 字节 */
    void write(Addr dpa, unsigned len, const uint8_t *data,
               DevWriteDone done) override;

    /** 被反压或在途已满之后又可以发请求时调用 */
    std::function<void()> onReady;

  private:
    /** 接设备内存的端口 */
    class MemPort : public RequestPort
    {
      public:
        /** 构造名为 name 的端口；owner 是所属的后端 */
        MemPort(const std::string &name, DevmemBackend &owner)
            : RequestPort(name), owner(owner) {}

      protected:
        /** timing 请求的响应 */
        bool recvTimingResp(PacketPtr pkt) override;

        /** 设备内存可以再接受请求 */
        void recvReqRetry() override;

      private:
        /** 所属的后端 */
        DevmemBackend &owner;
    };

    /** timing 请求携带的回调 */
    struct State : public Packet::SenderState
    {
        /** 读的回调 */
        DevReadDone rd;
        /** 写的回调 */
        DevWriteDone wr;
    };

    /** 发出一个 timing 请求；被反压时留待重试 */
    void send(PacketPtr pkt);

    /** 收到 timing 请求的响应 */
    void recvResp(PacketPtr pkt);

    /** 设备内存可以再接受请求 */
    void retry();

    /** 接设备内存的端口 */
    MemPort memPort;
    /** 访问设备内存时使用的请求者 ID */
    RequestorID requestorId;
    /** timing 请求的在途上限 */
    const unsigned maxOutstanding;
    /** 在途的 timing 请求数（含被反压的那一个） */
    unsigned outstanding = 0;
    /** 被反压、等待重试的请求 */
    PacketPtr blocked = nullptr;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR2_DEVMEM_BACKEND_HH__
