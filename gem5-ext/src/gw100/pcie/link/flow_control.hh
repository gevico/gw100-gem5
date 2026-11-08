/**
 * @file  flow_control.hh
 * @brief PCIe 累计信用字段的协商、计量与回绕
 *
 * @see spec-pcie
 */

#ifndef GW100_PCIE_LINK_FLOW_CONTROL_HH
#define GW100_PCIE_LINK_FLOW_CONTROL_HH

#include <cstdint>

namespace gem5::gw100::pcie
{

/** 一个头部或数据类别的发送信用，计数单位为原始 FC unit。 */
class CreditCounter
{
  public:
    /** 可保存的累计字段。 */
    struct State
    {
        /** 未缩放的线上字段宽度，头为 8，数据为 12。 */
        unsigned fieldBits = 8;
        /** Scale 编码，0/1/2/3。 */
        unsigned scale = 0;
        /** 已消耗累计信用。 */
        uint32_t consumed = 0;
        /** 最近合法广告的累计上限。 */
        uint32_t limit = 0;
        /** 已完成 InitFC 广告。 */
        bool initialized = false;
        /** 初始化时以零值广告无限信用。 */
        bool infinite = false;
    };

    /** 构造指定线上字段宽度的信用计数器。 */
    explicit CreditCounter(unsigned field_bits);
    /** 初始化或重新训练至 DL_Down 后复位，尚无可发送信用。 */
    void reset();
    /** 从 InitFC1 广告建立尺度和初始限额。 */
    bool initialize(unsigned advertised, unsigned scale);
    /** 接收累计 UpdateFC，拒绝尺度变化和不合法的倒退。 */
    bool update(unsigned advertised, unsigned scale);
    /** 当前是否允许消耗 units 个原始信用单位。 */
    bool canSpend(unsigned units) const;
    /** 原始 TLP 首次提交时扣除信用，重放不调用此函数。 */
    void spend(unsigned units);
    /** 可用原始信用单位，无限信用返回 UINT32_MAX。 */
    uint32_t available() const;
    /** 读取可保存状态。 */
    const State &
    state() const
    {
        return s;
    }
    /** 恢复已排空的累计计数状态。 */
    void restore(const State &state);

  private:
    /** Scale 对应的左移位数。 */
    unsigned shift() const;
    /** 累计计数的模数掩码。 */
    uint32_t mask() const;
    /** 完整累计状态。 */
    State s;
};

} // namespace gem5::gw100::pcie
#endif
