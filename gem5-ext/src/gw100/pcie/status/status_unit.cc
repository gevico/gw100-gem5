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

/**
 * 置 ERR_STATUS 的 bit，用于不由 BAR 访问引起的错误（DMA_HOST_ERR
 * 等）；它是第一次错误时 ERR_ADDR、ERR_INFO 记为 0（spec-pcie §6.3）
 */
void
StatusUnit::raiseErr(uint32_t bit)
{
    if (_errStatus == 0) {
        _errAddr = 0;
        _errInfo = 0;
    }
    _errStatus |= bit;
    updateIntStatus();
    DPRINTF(Gw100Pcie, "错误 %#x（非 BAR 访问）\n", bit);
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
    uint32_t old = _intStatus;
    _intStatus &= ~bits;
    // 源条件仍成立的位在同一周期重新置位，算一次事件（spec-pcie §8.4）
    uint32_t reassert = bits & old & (_intLevel | hwSource());
    _intStatus |= reassert;
    notify(reassert, old & ~_intStatus);
}

/** host 写 INT_SET：置位 bits，每个 1 位算一次中断事件（§8.4） */
void
StatusUnit::setInt(uint32_t bits)
{
    _intStatus |= bits;
    notify(bits, 0);
}

/** 固件写 INT_REQ：置位 bits，每个 1 位算一次中断事件（§6.5） */
void
StatusUnit::request(uint32_t bits)
{
    _intStatus |= bits;
    notify(bits, 0);
}

/** 写 INT_MASK */
void
StatusUnit::setIntMask(uint32_t v)
{
    _intMask = v;
    notify(0, 0);
}

/**
 * 硬件对各位源条件的贡献：ERROR 由 ERR_STATUS、DEV_FATAL 和 HDI 的
 * 错误寄存器决定
 */
uint32_t
StatusUnit::hwSource() const
{
    return (_errStatus || _fatal || _hdiErr) ? IntError : 0;
}

/** 硬件错误条件成立而 ERROR 为 0 时置位 ERROR（一次事件） */
void
StatusUnit::updateIntStatus()
{
    uint32_t set = hwSource() & ~_intStatus;
    _intStatus |= set;
    if (set)
        notify(set, 0);
}

/** 通知 onIntEvent */
void
StatusUnit::notify(uint32_t events, uint32_t cleared)
{
    if (onIntEvent)
        onIntEvent(events, cleared);
}

/**
 * DEV_STATUS.DEV_FATAL 的电平：INT_STATUS.ERROR 的硬件源之一
 * （spec-pcie §8.4）。它由 BootCtrl 保持，不属于本类的寄存器
 */
void
StatusUnit::setFatal(bool f)
{
    _fatal = f;
    updateIntStatus();
}

/**
 * HDI 影子寄存器中 HDI_FATAL ≠ 0 或 HDI_CTX_ERR ≠ 0 的电平：
 * INT_STATUS.ERROR 的硬件源之一（spec-pcie §8.4），由 HdiCtrl 保持
 */
void
StatusUnit::setHdiErr(bool e)
{
    _hdiErr = e;
    updateIntStatus();
}

/**
 * 把全部寄存器恢复为复位值；DEV_FATAL 的电平不变，仍成立时
 * INT_STATUS.ERROR 随即重新置位
 */
void
StatusUnit::reset()
{
    _intStatus = 0;
    _intMask = 0xFFFFFFFF;
    _errStatus = 0;
    _errAddr = 0;
    _errInfo = 0;
    _intLevel = 0;
    updateIntStatus();
    notify(0, 0);
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
    SERIALIZE_SCALAR(_fatal);
    SERIALIZE_SCALAR(_hdiErr);
    SERIALIZE_SCALAR(_intLevel);
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
    UNSERIALIZE_SCALAR(_fatal);
    UNSERIALIZE_SCALAR(_hdiErr);
    UNSERIALIZE_SCALAR(_intLevel);
}

} // namespace gw100
} // namespace gem5
