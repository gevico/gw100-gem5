/**
 * @file  mem_streamer.hh
 * @brief 功能单元访存的基础件：UnitPort 与 MemStreamer
 *
 * UnitPort：功能单元的请求端口，可被多个 MemStreamer 共用，按先后顺序发送，处理重试，
 *           并把响应交还给发出请求的 MemStreamer。
 * MemStreamer：把一个张量的字节区间（acis::ByteRun）拆成不超过 reqBytes、且不跨
 *           reqBytes 对齐边界的请求，限制同时在途的请求数。读时把数据按逻辑顺序收进
 *           紧密排列的缓冲区；写时从缓冲区取数据。
 */

#ifndef __ACIS_COMMON_MEM_STREAMER_HH__
#define __ACIS_COMMON_MEM_STREAMER_HH__

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "mem/packet.hh"
#include "mem/port.hh"
#include "sim/clocked_object.hh"
#include "src/common/fc.hh"

namespace gem5
{
namespace acis
{

class MemStreamer;

class UnitPort : public RequestPort
{
  public:
    UnitPort(const std::string &name, ClockedObject &unit);

    /** 排队发送；端口被下游阻塞时等重试再发。 */
    void send(PacketPtr pkt);

  protected:
    bool recvTimingResp(PacketPtr pkt) override;
    void recvReqRetry() override;

  private:
    void trySend();

    ClockedObject &unit;
    std::deque<PacketPtr> pending;
    bool blocked = false;
};

class MemStreamer
{
  public:
    struct ReqState : public Packet::SenderState
    {
        MemStreamer *streamer;
        uint64_t denseOff;
        ReqState(MemStreamer *s, uint64_t off) : streamer(s), denseOff(off) {}
    };

    /** 按地址选择端口：例如 DMA 访问本 Tile SRAM 走本地端口，其他地址走 NoC。 */
    using PortSelect = std::function<UnitPort *(Addr)>;

    MemStreamer(const std::string &name, RequestorID id, unsigned req_bytes,
                unsigned max_outstanding);

    /**
     * 开始读：runs 按逻辑顺序读入 dense（调用方保证容量足够）。
     * on_progress 在"从头开始已到齐的连续字节数"增加时调用，on_done 在全部到齐后调用。
     */
    void startRead(std::vector<ByteRun> runs, uint8_t *dense, PortSelect sel,
                   std::function<void()> on_progress, std::function<void()> on_done);

    /**
     * 开始写：把 dense 按逻辑顺序写到 runs。limit 非空时，只发出 dense 偏移小于
     * *limit 的数据（用于 DMA 边读边写）；limit 增长后调用 kick()。
     */
    void startWrite(std::vector<ByteRun> runs, const uint8_t *dense, PortSelect sel,
                    std::function<void()> on_done, const uint64_t *limit = nullptr);

    void kick() { issue(); }
    void handleResp(PacketPtr pkt);

    bool busy() const { return active; }
    uint64_t prefix() const { return donePrefix; }
    uint64_t totalBytes() const { return total; }
    const std::string &name() const { return _name; }

  private:
    void reset(std::vector<ByteRun> runs, PortSelect sel, bool is_read);
    void issue();
    void finishIfDone();

    const std::string _name;
    const RequestorID requestorId;
    const unsigned reqBytes;
    const unsigned maxOutstanding;

    bool active = false;
    bool isRead = true;
    std::vector<ByteRun> runs;
    PortSelect select;
    uint8_t *readBuf = nullptr;
    const uint8_t *writeBuf = nullptr;
    const uint64_t *limit = nullptr;
    std::function<void()> onProgress;
    std::function<void()> onDone;

    size_t runIdx = 0;
    uint64_t runOff = 0;     // 当前区间内已发出的字节数
    uint64_t issuedOff = 0;  // 紧密缓冲区中已发出的字节数
    uint64_t total = 0;
    unsigned outstanding = 0;

    uint64_t donePrefix = 0;                  // 从 0 开始已完成的连续字节数
    std::map<uint64_t, uint64_t> doneRanges;  // 前缀之后、已完成但不连续的区间
};

} // namespace acis
} // namespace gem5

#endif // __ACIS_COMMON_MEM_STREAMER_HH__
