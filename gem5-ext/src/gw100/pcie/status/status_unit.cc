/**
 * @file  status_unit.cc
 * @brief StatusUnit 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/status/status_unit.hh"

#include "base/trace.hh"
#include "debug/Gw100Pcie.hh"
#include "sim/cur_tick.hh"

namespace gem5
{
namespace gw100
{

/**
 * 置 ERR_STATUS 的 bit；ERR_STATUS 原为 0 时锁存这次错误的偏移和
 * 访问信息
 */
void
StatusUnit::logError(uint32_t bit, Addr off, bool is_write, unsigned size,
                     unsigned bar)
{
    // 只记录 ERR_STATUS 从全 0 变为非 0 时的那一次（spec-pcie §6.3）
    if (_errStatus == 0) {
        _errAddr = uint32_t(off);
        _errInfo = (is_write ? 1u : 0u) | ((size & 0xF) << 2) |
                   ((bar & 0x3) << 6);
    }
    _errStatus |= bit;
    updateIntStatus();
    DPRINTF(Gw100Pcie, "错误 %#x：BAR%u 偏移 %#x，%s %u 字节\n", bit, bar,
            off, is_write ? "写" : "读", size);
}

/** 写 1 清零 ERR_STATUS 中 bits 指定的位 */
void
StatusUnit::clearErr(uint32_t bits)
{
    _errStatus &= ~bits;
    updateIntStatus();
}

/** 写 1 清零 INT_STATUS 中 bits 指定的位；错误仍在时 ERROR 重新置位 */
void
StatusUnit::clearInt(uint32_t bits)
{
    _intStatus &= ~bits;
    updateIntStatus();
}

/** 错误条件仍然成立时置 INT_STATUS.ERROR */
void
StatusUnit::updateIntStatus()
{
    // 错误条件仍然成立时，ERROR 位清零后立即重新置位（spec-pcie §8.4）
    if (_errStatus)
        _intStatus |= IntError;
}

/** 把全部状态恢复为复位值 */
void
StatusUnit::reset()
{
    _intStatus = 0;
    _intMask = 0xFFFFFFFF;
    _errStatus = 0;
    _errAddr = 0;
    _errInfo = 0;
}

/** 保存全部状态 */
void
StatusUnit::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(_intStatus);
    SERIALIZE_SCALAR(_intMask);
    SERIALIZE_SCALAR(_errStatus);
    SERIALIZE_SCALAR(_errAddr);
    SERIALIZE_SCALAR(_errInfo);
}

/** 恢复 serialize() 保存的状态 */
void
StatusUnit::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(_intStatus);
    UNSERIALIZE_SCALAR(_intMask);
    UNSERIALIZE_SCALAR(_errStatus);
    UNSERIALIZE_SCALAR(_errAddr);
    UNSERIALIZE_SCALAR(_errInfo);
}

} // namespace gw100
} // namespace gem5
