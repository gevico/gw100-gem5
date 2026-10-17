/**
 * @file  tile_controller.hh
 * @brief Tile 控制器：接收张量指令，换算本地地址后分发到各功能单元
 *
 * - DMA_COPY、VEC_*、MATMUL 分别进入 DMA、向量引擎、矩阵引擎的队列；同一单元内按序执行，
 *   不同单元之间乱序，依赖关系由同步计数器表达。
 * - NOP、SYNC、END 由控制器自己按序处理：SYNC 只做等待和通知；END 要等本 Tile 所有
 *   单元空闲后，向管理 Tile 报告完成。
 * - 队列空位通过 reserve() 预留：派发器在指令发出时预留，指令到达时占用。
 * - 指令到达时先做 Tile 相对地址换算：落在本地窗口内的操作数地址改成本 Tile SRAM
 *   的全局地址，功能单元只看到全局地址。
 *
 * @see SPEC-200, SPEC-230
 */

#ifndef __ACIS_COMPUTE_TILE_CONTROLLER_TILE_CONTROLLER_HH__
#define __ACIS_COMPUTE_TILE_CONTROLLER_TILE_CONTROLLER_HH__

#include <array>
#include <deque>
#include <string>

#include "base/statistics.hh"
#include "params/AcisTileController.hh"
#include "sim/clocked_object.hh"
#include "src/common/fc.hh"

namespace gem5
{
namespace acis
{

class Dispatcher;
class FunctionalUnit;
class SyncUnit;

class TileController : public ClockedObject
{
  public:
    PARAMS(AcisTileController);
    TileController(const Params &p);

    void init() override;
    void setDispatcher(Dispatcher *d) { dispatcher = d; }
    unsigned tileId() const { return _tileId; }

    /** unit 的队列是否还能预留空位（不改变状态）。 */
    bool canReserve(isa::Unit unit) const;
    /** 为 unit 预留一个队列空位；调用前须确认 canReserve() 为真。 */
    void reserve(isa::Unit unit);
    /** 指令到达（此前必须已为其预留空位）。 */
    void deliver(const TensorInst &inst);
    /** 功能单元退休一条指令时调用。 */
    void unitRetired(FunctionalUnit *u);
    /**
     * 按本 Tile 换算指令中的本地窗口地址。操作数越出本 Tile SRAM 时返回 false；
     * 派发器在校验程序时也用它。
     */
    bool resolve(TensorInst &inst, std::string &err) const;

  private:
    FunctionalUnit *unitFor(isa::Unit u) const;
    void processCtrl();
    bool allUnitsIdle() const;

    const unsigned _tileId;
    const LocalWindow window;
    SyncUnit *sync;
    std::array<FunctionalUnit *, 4> units{{nullptr, nullptr, nullptr, nullptr}};
    std::array<unsigned, 4> reserved{{0, 0, 0, 0}};
    Dispatcher *dispatcher = nullptr;

    const size_t ctrlDepth;
    std::deque<TensorInst> ctrlQueue;
    bool ctrlBusy = false;

    struct CtrlStats : public statistics::Group
    {
        CtrlStats(TileController &c);
        statistics::Scalar delivered;
        statistics::Scalar ends;
    } ctrlStats;
};

} // namespace acis
} // namespace gem5

#endif // __ACIS_COMPUTE_TILE_CONTROLLER_TILE_CONTROLLER_HH__
