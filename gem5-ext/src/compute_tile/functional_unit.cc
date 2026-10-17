#include "src/compute_tile/functional_unit.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/AcisUnit.hh"
#include "sim/eventq.hh"
#include "sim/system.hh"
#include "src/compute_tile/controller/tile_controller.hh"
#include "src/compute_tile/sync/sync_unit.hh"

namespace gem5
{
namespace acis
{

FunctionalUnit::FunctionalUnit(const Params &p)
    : ClockedObject(p), functional(p.functional_compute), sync(p.sync),
      requestorId(p.system->getRequestorId(this)), queueDepth(p.queue_depth),
      unitStats(*this)
{
}

void
FunctionalUnit::push(const TensorInst &inst)
{
    panic_if(queue.size() >= queueDepth, "%s: 指令队列已满", name());
    queue.push_back(inst);
    tryStart();
}

void
FunctionalUnit::tryStart()
{
    if (running || queue.empty())
        return;
    running = true;
    const TensorInst &inst = queue.front();
    if (inst.waitEn) {
        waitStart = curTick();
        sync->wait(inst.waitCnt, inst.waitVal, [this]() {
            unitStats.waitTicks += curTick() - waitStart;
            begin();
        });
    } else {
        begin();
    }
}

void
FunctionalUnit::begin()
{
    startTick = curTick();
    DPRINTF(AcisUnit, "开始 %s\n", queue.front().toString());
    execute(queue.front());
}

void
FunctionalUnit::finish()
{
    panic_if(!running || queue.empty(), "%s: 没有正在执行的指令", name());
    const TensorInst inst = queue.front();
    queue.pop_front();
    unitStats.insts++;
    unitStats.busyTicks += curTick() - startTick;
    DPRINTF(AcisUnit, "完成 %s，耗时 %llu ticks\n", inst.toString(), curTick() - startTick);

    if (inst.sigEn) {
        panic_if(inst.sigTile != isa::kLocalTile,
                 "%s: v0 尚未实现跨 Tile 通知（目标 Tile %u）", name(), inst.sigTile);
        sync->signal(inst.sigCnt);
    }
    running = false;
    ctrl->unitRetired(this);
    // 下一条指令从下一个周期开始
    schedule(new EventFunctionWrapper([this]() { tryStart(); }, name() + ".next", true),
             clockEdge(Cycles(1)));
}

FunctionalUnit::UnitStats::UnitStats(FunctionalUnit &u)
    : statistics::Group(&u, "unit"),
      ADD_STAT(insts, statistics::units::Count::get(), "执行的指令数"),
      ADD_STAT(busyTicks, statistics::units::Tick::get(), "执行指令的时间"),
      ADD_STAT(waitTicks, statistics::units::Tick::get(), "等待同步计数器的时间")
{
}

} // namespace acis
} // namespace gem5
