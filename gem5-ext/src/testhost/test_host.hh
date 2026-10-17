/**
 * 测试平台（不是硬件）：在没有驱动和 host 模型的阶段代替 host 跑算子测试。
 *
 * 1. 后门加载：仿真开始时用功能访问把各个输入文件写到指定地址，不占仿真时间；
 * 2. 前门启动：像驱动一样，用时序请求写 CMD_ADDR 和 DOORBELL；
 * 3. 轮询完成记录，直到 status 不为 0；
 * 4. 后门读回：把指定地址区间写成文件，再写一个 JSON 结果文件，然后结束仿真。
 * 超过 timeout 仍未完成时报错退出。
 */

#ifndef __ACIS_TESTHOST_TEST_HOST_HH__
#define __ACIS_TESTHOST_TEST_HOST_HH__

#include <string>
#include <vector>

#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/AcisTestHost.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"

namespace gem5
{
namespace acis
{

class TestHost : public ClockedObject
{
  private:
    class HostPort : public RequestPort
    {
      public:
        HostPort(const std::string &name, TestHost &h) : RequestPort(name), host(h) {}

      protected:
        bool recvTimingResp(PacketPtr pkt) override { return host.handleResp(pkt); }
        void recvReqRetry() override { host.retry(); }

      private:
        TestHost &host;
    };

    enum class Phase { WriteCmdAddr, Doorbell, Polling, Finished };

  public:
    PARAMS(AcisTestHost);
    TestHost(const Params &p);

    Port &getPort(const std::string &if_name, PortID idx = InvalidPortID) override;
    void startup() override;

  private:
    void functionalAccess(Addr addr, uint8_t *data, uint64_t bytes, bool write);
    void loadAll();
    void sendTiming(PacketPtr pkt);
    bool handleResp(PacketPtr pkt);
    void retry();
    void writeReg(Addr addr, uint64_t value, unsigned bytes);
    void poll();
    void finishRun(const uint8_t *cpl);
    void timeout();

    HostPort port;
    const RequestorID requestorId;
    const std::vector<std::string> loads;
    const std::vector<std::string> dumps;
    const Addr mgmtBase;
    const Addr cmdRecordAddr;
    const Addr completionAddr;
    const Tick pollInterval;
    const Tick timeoutTicks;
    const std::string resultFile;
    const uint64_t chunkBytes;

    Phase phase = Phase::WriteCmdAddr;
    PacketPtr blockedPkt = nullptr;
    EventFunctionWrapper kickEvent;
    EventFunctionWrapper pollEvent;
    EventFunctionWrapper timeoutEvent;
};

} // namespace acis
} // namespace gem5

#endif // __ACIS_TESTHOST_TEST_HOST_HH__
