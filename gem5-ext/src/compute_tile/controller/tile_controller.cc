/**
 * @file  tile_controller.cc
 * @brief TileController 的实现
 *
 * @see SPEC-200, SPEC-230
 */

#include "src/compute_tile/controller/tile_controller.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/AcisCtrl.hh"
#include "sim/eventq.hh"
#include "src/compute_tile/dma/dma_engine.hh"
#include "src/compute_tile/functional_unit.hh"
#include "src/compute_tile/sync/sync_unit.hh"
#include "src/compute_tile/vector/vector_engine.hh"
#include "src/mgmt_tile/dispatcher.hh"

namespace gem5
{
namespace acis
{

TileController::TileController(const Params &p)
    : ClockedObject(p), _tileId(p.tile_id),
      window{p.local_window.start(), p.local_window.size(), p.sram_range.start(),
             p.sram_range.size()},
      sync(p.sync), ctrlDepth(p.ctrl_queue_depth), ctrlStats(*this)
{
    units[static_cast<unsigned>(isa::Unit::Dma)] = p.dma;
    units[static_cast<unsigned>(isa::Unit::Ve)] = p.ve;
    units[static_cast<unsigned>(isa::Unit::Me)] = p.me;  // v0 可以为空
}

void
TileController::init()
{
    ClockedObject::init();
    for (auto *u : units) {
        if (u)
            u->setController(this);
    }
}

FunctionalUnit *
TileController::unitFor(isa::Unit u) const
{
    return units[static_cast<unsigned>(u)];
}

bool
TileController::canReserve(isa::Unit u) const
{
    const unsigned i = static_cast<unsigned>(u);
    if (u == isa::Unit::Ctrl)
        return ctrlQueue.size() + reserved[i] < ctrlDepth;
    const FunctionalUnit *fu = units[i];
    panic_if(!fu, "%s: 本 Tile 没有执行该类指令的单元（unit %u）", name(), i);
    return fu->occupancy() + reserved[i] < fu->depth();
}

void
TileController::reserve(isa::Unit u)
{
    panic_if(!canReserve(u), "%s: 队列已满，不能预留", name());
    reserved[static_cast<unsigned>(u)]++;
}

bool
TileController::resolve(TensorInst &inst, std::string &err) const
{
    return resolveLocal(inst, window, err);
}

void
TileController::deliver(const TensorInst &inst)
{
    const isa::Unit u = inst.unit();
    const unsigned i = static_cast<unsigned>(u);
    panic_if(reserved[i] == 0, "%s: 指令到达前没有预留队列空位", name());
    reserved[i]--;
    ctrlStats.delivered++;
    DPRINTF(AcisCtrl, "收到 %s\n", inst.toString());
    if (u == isa::Unit::Ctrl) {
        ctrlQueue.push_back(inst);
        processCtrl();
    } else {
        TensorInst local = inst;
        std::string err;
        // 派发器已经校验过，这里不应失败
        panic_if(!resolve(local, err), "%s: %s", name(), err);
        units[i]->push(local);
    }
}

bool
TileController::allUnitsIdle() const
{
    for (auto *u : units) {
        if (u && !u->idle())
            return false;
    }
    return true;
}

void
TileController::unitRetired(FunctionalUnit *)
{
    if (dispatcher)
        dispatcher->creditAvailable();
    processCtrl();
}

void
TileController::processCtrl()
{
    while (!ctrlBusy && !ctrlQueue.empty()) {
        const TensorInst inst = ctrlQueue.front();
        switch (inst.opcode) {
          case isa::Opcode::NOP:
            break;
          case isa::Opcode::END:
            // 等本 Tile 所有单元空闲；功能单元退休时会再次进入这里
            if (!allUnitsIdle())
                return;
            ctrlStats.ends++;
            DPRINTF(AcisCtrl, "END：本 Tile 执行完毕\n");
            dispatcher->tileDone(_tileId);
            break;
          case isa::Opcode::SYNC:
            if (inst.waitEn) {
                ctrlBusy = true;
                sync->wait(inst.waitCnt, inst.waitVal, [this, inst]() {
                    ctrlBusy = false;
                    if (inst.sigEn)
                        sync->signal(inst.sigCnt);
                    ctrlQueue.pop_front();
                    if (dispatcher)
                        dispatcher->creditAvailable();
                    processCtrl();
                });
                return;
            }
            if (inst.sigEn)
                sync->signal(inst.sigCnt);
            break;
          default:
            panic("%s: %s 不是控制指令", name(), opcodeName(inst.opcode));
        }
        ctrlQueue.pop_front();
        if (dispatcher)
            dispatcher->creditAvailable();
    }
}

TileController::CtrlStats::CtrlStats(TileController &c)
    : statistics::Group(&c, "ctrl"),
      ADD_STAT(delivered, statistics::units::Count::get(), "收到的指令数"),
      ADD_STAT(ends, statistics::units::Count::get(), "执行完的 END 数")
{
}

} // namespace acis
} // namespace gem5
