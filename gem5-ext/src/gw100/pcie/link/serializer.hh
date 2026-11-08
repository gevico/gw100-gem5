/**
 * @file  serializer.hh
 * @brief PCIe 跨包相位、编码块和维护流量的精确串行时钟
 *
 * 内部以半皮秒表示全部 Gen1–Gen4 位边沿，只有观测事件向上量化到
 * 1 ps。调用者负责在规定调度点调用 skip()，包内传输不被抢占。
 *
 * @see spec-pcie
 */

#ifndef GW100_PCIE_LINK_SERIALIZER_HH
#define GW100_PCIE_LINK_SERIALIZER_HH

#include <cstdint>

namespace gem5::gw100::pcie
{

/** 一次串行占用，首尾保留精确相位。 */
struct FrameTime
{
    /** 首个包或 Ordered Set 符号起始，单位 0.5 ps。 */
    uint64_t beginHalf = 0;
    /** 最后一个位结束，单位 0.5 ps。 */
    uint64_t endHalf = 0;
    /** 本次编码块头占用，单位 0.5 ps。 */
    uint64_t encodingHalf = 0;
    /** 本次 EDS 与块尾填充，单位 0.5 ps。 */
    uint64_t paddingHalf = 0;
    /** Ordered Set 串行时间，单位 0.5 ps。 */
    uint64_t orderedHalf = 0;
    /** 训练序列期间插入的 SKP 数。 */
    unsigned skpCount = 0;
    /** 首符号事件，单位 ps。 */
    uint64_t
    beginTick() const
    {
        return (beginHalf + 1) / 2;
    }
    /** 包尾事件，单位 ps。 */
    uint64_t
    endTick() const
    {
        return (endHalf + 1) / 2;
    }
};

/** 单方向的串行相位和编码状态。 */
class Serializer
{
  public:
    /** 排空后可保存的串行器状态。 */
    struct State
    {
        /** 当前代际，1–4。 */
        unsigned generation = 4;
        /** 当前 lane 数，1/2/4。 */
        unsigned width = 4;
        /** 下一个位边沿的串行位周期，单位 0.5 ps。 */
        uint64_t bitHalf = 125;
        /** 最近一次传输包尾，单位 0.5 ps。 */
        uint64_t endHalf = 0;
        /** 数据流起点，随插入的 Ordered Set 推进，单位 0.5 ps。 */
        uint64_t dataOriginHalf = 0;
        /** 本数据流已发送的每 lane 数据符号数，包含 IDL 和 EDS。 */
        uint64_t dataSymbols = 0;
        /** 下次 SKP 调度时刻，单位 0.5 ps。 */
        uint64_t nextSkpHalf = 0;
        /** 当前是否处于数据流。 */
        bool dataStream = false;
        /** 物理发送端已离开电气空闲，训练期间也保持。 */
        bool active = false;
        /** 下次 Gen4 SKP 是否为 Control 类型。 */
        bool controlSkpNext = false;
        /** 上次 EIEOS 或规定接收检查点之后已发出的 TS 数。 */
        unsigned trainingCount = 0;
    };

    /** 构造指定速率和宽度的单方向串行器。 */
    Serializer(unsigned generation, unsigned width, uint64_t origin_ps = 0);
    /** 物理链路重新建立或换速率时重建串行相位。 */
    void reset(unsigned generation, unsigned width, uint64_t origin_ps);
    /** 开始新数据流，计入 SDS 和 Gen4 初始 Control SKP。 */
    FrameTime beginStream(uint64_t earliest_ps);
    /** 发送含完整 framing 的包，长度须为完整 DWORD，单位 B。 */
    FrameTime packet(unsigned bytes, uint64_t earliest_ps);
    /** 在当前包尾之后发送一个周期 SKP，包含 EDS 和必要块尾填充。 */
    FrameTime skip(uint64_t earliest_ps);
    /** 在数据流以外发送 TS，满足数量和持续时间并计入 SKP/EIEOS。 */
    FrameTime training(unsigned sets, uint64_t earliest_ps,
                       uint64_t minimum_duration_ps = 0);
    /** 收到规定的首个 TS2 时重置 EIEOS 的 TS 间隔计数。 */
    void
    resetTrainingCount()
    {
        s.trainingCount = 0;
    }
    /** 结束数据流并发送 Recovery.RcvrLock 所需的 EIEOS。 */
    FrameTime beginRecovery(uint64_t earliest_ps);
    /** 发送 EIOS 并结束当前数据流，保留电气空闲的串行起点。 */
    FrameTime electricalIdle(uint64_t earliest_ps);
    /** 下一次 SKP 调度事件，单位 ps。 */
    uint64_t
    nextSkpTick() const
    {
        return (s.nextSkpHalf + 1) / 2;
    }
    /** 最近串行传输完成事件，单位 ps。 */
    uint64_t
    endTick() const
    {
        return (s.endHalf + 1) / 2;
    }
    /** 读取排空后可保存的相位状态。 */
    const State &
    state() const
    {
        return s;
    }
    /** 恢复已验证、已排空的相位状态。 */
    void restore(const State &state);

  private:
    /** 某个数据符号开始的绝对相位，单位 0.5 ps。 */
    uint64_t symbolStart(uint64_t symbol) const;
    /** 前 count 个数据符号结束的绝对相位，单位 0.5 ps。 */
    uint64_t symbolsEnd(uint64_t count) const;
    /** 填入 IDL，找到不早于指定相位的下一完整符号。 */
    void idleUntil(uint64_t half_ps);
    /** 标准 SKP 的物理长度，单位 bit/lane。 */
    unsigned skpBits() const;
    /** SKP 调度周期，单位 0.5 ps。 */
    uint64_t skpIntervalHalf() const;
    /** 串行器的完整标量状态。 */
    State s;
};

} // namespace gem5::gw100::pcie
#endif
