/**
 * @file  bar0_handler.cc
 * @brief Bar0Handler 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/bar0/bar0_handler.hh"

#include <cstring>

#include "base/logging.hh"

namespace gem5
{
namespace gw100
{

/** 登记一个寄存器块；块的起始偏移必须按 BlockSize 对齐且互不重复 */
void
Bar0Handler::addBlock(RegBlock &block)
{
    const Addr base = block.base();
    panic_if(base % BlockSize != 0, "Bar0Handler: 块偏移 %#x 未对齐", base);
    panic_if(blocks.count(base), "Bar0Handler: 块偏移 %#x 重复", base);
    blocks[base] = &block;
}

/** BAR0 偏移 off 所在的寄存器块；没有登记时返回 nullptr */
RegBlock *
Bar0Handler::blockAt(Addr off) const
{
    auto it = blocks.find(off & ~(BlockSize - 1));
    return it == blocks.end() ? nullptr : it->second;
}

/** [off, off + size) 覆盖的每个 32 位字是否都是已实现的寄存器 */
bool
Bar0Handler::implemented(Addr off, unsigned size) const
{
    for (Addr w = off & ~Addr(3); w < off + size; w += 4) {
        const RegBlock *b = blockAt(w);
        if (!b || !b->implemented(w))
            return false;
    }
    return true;
}

/**
 * 处理一次访问：先按 spec-pcie §5.2 检查宽度和偏移，出错时记录并
 * 以成功完成结束；合法访问分派到对应的寄存器块
 */
Tick
Bar0Handler::access(BarAccess &a)
{
    const bool aligned = (a.off % a.size) == 0;
    const bool width_ok = a.write ? (a.size == 4 || a.size == 8) && aligned
                                  : (a.size == 1 || a.size == 2 ||
                                     a.size == 4 || a.size == 8) && aligned;

    if (!width_ok || !implemented(a.off, a.size)) {
        status.logError(width_ok ? StatusUnit::ErrRsvdAccess
                                 : StatusUnit::ErrBadWidth,
                        a.off, a.write, a.size, 0);
        if (!a.write)
            std::memset(a.data, 0, a.size);
        return pioDelay;
    }

    RegBlock *b = blockAt(a.off);
    if (a.write)
        b->write(a.off, a.data, a.size);
    else
        b->read(a.off, a.data, a.size);
    return pioDelay;
}

} // namespace gw100
} // namespace gem5
