/**
 * 功能单元的公共基类（SPEC-200）。
 *
 * 每个单元有自己的指令队列，单元内按序执行；执行前若指令要求等待，先向同步单元等待
 * 计数器；执行完若指令要求通知，则更新计数器，再告诉 Tile 控制器"一条指令退休了"。
 * 派生类只需实现 execute()，结束时调用 finish()。
 */

#ifndef __ACIS_COMPUTE_TILE_FUNCTIONAL_UNIT_HH__
#define __ACIS_COMPUTE_TILE_FUNCTIONAL_UNIT_HH__

#include <deque>

#include "base/statistics.hh"
#include "mem/request.hh"
#include "params/AcisFunctionalUnit.hh"
#include "sim/clocked_object.hh"
#include "src/common/fc.hh"

namespace gem5
{
namespace acis
{

class SyncUnit;
class TileController;

class FunctionalUnit : public ClockedObject
{
  public:
    PARAMS(AcisFunctionalUnit);
    FunctionalUnit(const Params &p);

    void setController(TileController *c) { ctrl = c; }

    /** 调用方（Tile 控制器）负责保证队列有空位。 */
    void push(const TensorInst &inst);

    size_t occupancy() const { return queue.size(); }
    size_t depth() const { return queueDepth; }
    bool idle() const { return queue.empty() && !running; }

  protected:
    /** 开始执行队首指令；执行完必须调用 finish()。 */
    virtual void execute(const TensorInst &inst) = 0;
    void finish();

    /** 是否真正计算结果；只跑时序时关闭以加快仿真。 */
    const bool functional;
    SyncUnit *sync;
    TileController *ctrl = nullptr;
    const RequestorID requestorId;

  private:
    void tryStart();
    void begin();

    const size_t queueDepth;
    std::deque<TensorInst> queue;
    bool running = false;
    Tick startTick = 0;

    struct UnitStats : public statistics::Group
    {
        UnitStats(FunctionalUnit &u);
        statistics::Scalar insts;
        statistics::Scalar busyTicks;
        statistics::Scalar waitTicks;
    } unitStats;
    Tick waitStart = 0;
};

} // namespace acis
} // namespace gem5

#endif // __ACIS_COMPUTE_TILE_FUNCTIONAL_UNIT_HH__
