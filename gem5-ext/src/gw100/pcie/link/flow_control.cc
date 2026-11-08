/**
 * @file  flow_control.cc
 * @brief PCIe 累计信用计数的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/link/flow_control.hh"

#include <stdexcept>

namespace gem5::gw100::pcie
{

/** 构造指定线上字段宽度的信用计数器。 */
CreditCounter::CreditCounter(unsigned field_bits)
{
    if (field_bits != 8 && field_bits != 12) {
        throw std::invalid_argument("PCIe credit field width");
    }
    s.fieldBits = field_bits;
}

/** 初始化或重新训练至 DL_Down 后复位，尚无可发送信用。 */
void
CreditCounter::reset()
{
    unsigned bits = s.fieldBits;
    s = State{};
    s.fieldBits = bits;
}

/** Scale 对应的左移位数。 */
unsigned
CreditCounter::shift() const
{
    return s.scale < 2 ? 0 : (s.scale - 1) * 2;
}

/** 累计计数的模数掩码。 */
uint32_t
CreditCounter::mask() const
{
    return (1u << (s.fieldBits + shift())) - 1;
}

/** 从 InitFC1 广告建立尺度和初始限额。 */
bool
CreditCounter::initialize(unsigned advertised, unsigned scale)
{
    if (s.initialized || scale > 3 || advertised >= (1u << s.fieldBits)) {
        return false;
    }
    // 最大初始广告受半模数约束，零值在 InitFC 中表示无限。
    if (advertised >= (1u << (s.fieldBits - 1))) {
        return false;
    }
    s.scale = scale;
    s.limit = advertised << shift();
    s.infinite = advertised == 0;
    s.initialized = true;
    return true;
}

/** 接收累计 UpdateFC，拒绝尺度变化和不合法的倒退。 */
bool
CreditCounter::update(unsigned advertised, unsigned scale)
{
    if (!s.initialized || scale != s.scale ||
        advertised >= (1u << s.fieldBits)) {
        return false;
    }
    if (s.infinite) {
        return advertised == 0;
    }
    uint32_t limit = advertised << shift();
    uint32_t advance = (limit - s.limit) & mask();
    uint32_t unused = (limit - s.consumed) & mask();
    if (advance > mask() / 2 || unused > mask() / 2) {
        return false;
    }
    s.limit = limit;
    return true;
}

/** 当前是否允许消耗 units 个原始信用单位。 */
bool
CreditCounter::canSpend(unsigned units) const
{
    if (!s.initialized) {
        return false;
    }
    if (s.infinite) {
        return true;
    }
    if (units > mask() / 2) {
        return false;
    }
    uint32_t required = (s.consumed + units) & mask();
    return ((s.limit - required) & mask()) <= (mask() + 1) / 2;
}

/** 原始 TLP 首次提交时扣除信用，重放不调用此函数。 */
void
CreditCounter::spend(unsigned units)
{
    if (!canSpend(units)) {
        throw std::logic_error("PCIe credit overcommit");
    }
    s.consumed = (s.consumed + units) & mask();
}

/** 可用原始信用单位，无限信用返回 UINT32_MAX。 */
uint32_t
CreditCounter::available() const
{
    if (!s.initialized) {
        return 0;
    }
    if (s.infinite) {
        return UINT32_MAX;
    }
    return (s.limit - s.consumed) & mask();
}

/** 恢复已排空的累计计数状态。 */
void
CreditCounter::restore(const State &state)
{
    if (state.fieldBits != s.fieldBits || state.scale > 3) {
        throw std::invalid_argument("PCIe credit checkpoint fields");
    }
    s = state;
    if (s.consumed > mask() || s.limit > mask() ||
        (!s.infinite && s.initialized && available() > mask() / 2)) {
        throw std::invalid_argument("PCIe credit checkpoint counters");
    }
}

} // namespace gem5::gw100::pcie
