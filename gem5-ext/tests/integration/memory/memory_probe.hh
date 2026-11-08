/**
 * @file  memory_probe.hh
 * @brief Memory Tile 组件测试的有界 timing 请求源
 *
 * @see spec-dram
 */
#ifndef GW100_MEMORY_PROBE_HH
#define GW100_MEMORY_PROBE_HH

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include "mem/port.hh"
#include "params/Gw100MemoryProbe.hh"
#include "sim/clocked_object.hh"
#include "sim/serialize.hh"

namespace gem5::gw100
{

/** 顺序脚本、并行请求和独立数据参考 */
class Gw100MemoryProbe : public ClockedObject
{
  public:
    PARAMS(Gw100MemoryProbe);
    /** 绑定测试操作和三个硬件来源 */
    explicit Gw100MemoryProbe(const Params &p);
    /** 查找三个来源端口 */
    Port &getPort(const std::string &name,
                  PortID idx = InvalidPortID) override;
    /** 驱动 CRC 输入并启动脚本 */
    void startup() override;
    /** 只在没有在途请求时完成排空 */
    DrainState drain() override;
    /** 恢复 checkpoint 的后续脚本 */
    void drainResume() override;
    /** 保存已排空的脚本与独立参考 */
    void serialize(CheckpointOut &cp) const override;
    /** 恢复脚本和独立参考 */
    void unserialize(CheckpointIn &cp) override;
    /** 已比较的读数据字节数 */
    uint64_t
    checks() const
    { return checked; }
    /** 来源被反压的次数 */
    uint64_t
    retries() const
    { return rejected; }

  private:
    /** 请求携带的测试期望 */
    struct State : public Packet::SenderState
    {
        /** 脚本操作编码 */
        unsigned kind;
        /** 测试来源 */
        unsigned source;
        /** 操作地址 */
        Addr address;
        /** 数据模式或期望寄存器值 */
        uint64_t value;
        /** 创建时的测试代次 */
        uint64_t generation;
    };
    /** 一个测试来源端口 */
    class ProbePort : public RequestPort
    {
      public:
        /** 绑定测试源和来源编号 */
        ProbePort(const std::string &name, Gw100MemoryProbe &owner);
        /** 被反压时持有的单个请求 */
        PacketPtr blocked = nullptr;

      protected:
        /** 接收并比较响应 */
        bool recvTimingResp(PacketPtr pkt) override;
        /** 重试被拒绝的请求 */
        void recvReqRetry() override;

      private:
        /** 所属测试源 */
        Gw100MemoryProbe &owner;
        /** 已施加过背压的响应 */
        PacketPtr heldResponse = nullptr;
        /** 通知设备撤销响应背压 */
        EventFunctionWrapper retryResponse;
    };
    /** 执行当前脚本步骤 */
    void process();
    /** 比较一个响应并更新独立参考 */
    void response(PacketPtr pkt);
    /** 安排下一次处理 */
    void wake(Tick delay = 0);
    /** 三个来源端口 */
    std::array<std::unique_ptr<ProbePort>, 3> ports;
    /** 待执行的五元组 */
    const std::vector<uint64_t> operations;
    /** 在途上限 */
    const unsigned maximum;
    /** 每个响应的背压时间，tick */
    const Tick responseHold;
    /** 设备 System 请求者编号 */
    const RequestorID requestor;
    /** 当前脚本偏移 */
    uint64_t cursor = 0;
    /** 在途计数 */
    unsigned outstanding = 0;
    /** 独立的数据参考 */
    std::unordered_map<Addr, uint8_t> expected;
    /** 当前测试代次 */
    uint64_t generation = 0;
    /** 数据比较字节数 */
    uint64_t checked = 0;
    /** 请求反压次数 */
    uint64_t rejected = 0;
    /** 寄存器轮询在途 */
    bool polling = false;
    /** 轮询开始时刻 */
    Tick pollStarted = 0;
    /** 允许在途请求继续完成的脚本暂停期限，tick */
    Tick pauseUntil = 0;
    /** 正在全局排空 */
    bool draining = false;
    /** 测试处理事件 */
    EventFunctionWrapper event;
};

} // namespace gem5::gw100
#endif
