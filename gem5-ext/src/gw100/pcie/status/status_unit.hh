/**
 * @file  status_unit.hh
 * @brief ERR_STATUS 与 INT_STATUS：错误记录与中断源状态
 *
 * BAR0 前端、BAR2 窗口等组件发现错误时都调用 logError()；全局寄存器块
 * 只是这些状态的视图。中断投递在后续阶段实现，这里只维护状态位。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_STATUS_STATUS_UNIT_HH__
#define __GW100_PCIE_STATUS_STATUS_UNIT_HH__

#include <cstdint>
#include <string>

#include "base/types.hh"
#include "sim/serialize.hh"

namespace gem5
{
namespace gw100
{

/** 主机接口的错误状态与中断源状态（spec-pcie §6.3、§8.4） */
class StatusUnit : public Serializable
{
  public:
    /** ERR_STATUS 的位（spec-pcie §6.3） */
    enum : uint32_t
    {
        /** 访问了 BAR0 的保留偏移 */
        ErrRsvdAccess = 1u << 0,
        /** 以非法宽度访问寄存器 */
        ErrBadWidth = 1u << 1,
        /** 访问了未使能的 BAR2 窗口 */
        ErrWinDisabled = 1u << 2,
        /** BAR2 窗口的目标不在设备内存内，或写了只读窗口 */
        ErrWinTarget = 1u << 3,
        /** 写入 doorbell 的值越界（命令协议实现后使用） */
        ErrDbInvalid = 1u << 4,
        /** DMA 访问 host 内存出错（DMA 实现后使用） */
        ErrDmaHost = 1u << 5,
        /** 收到 poisoned TLP（链路建模后使用） */
        ErrRxPoisoned = 1u << 6,
        /** 启动窗口收到非法写入（启动过程建模后使用） */
        ErrBootWin = 1u << 7,
    };

    /** INT_STATUS 的位（spec-pcie §8.4） */
    enum : uint32_t
    {
        /** 管理完成队列有新的完成记录 */
        IntAdmin = 1u << 0,
        /** ERR_STATUS 非零等错误条件 */
        IntError = 1u << 1,
        /** 固件事件 */
        IntFwEvent = 1u << 2,
    };

    /** 构造；name 用于调试输出，取所属设备的名字 */
    explicit StatusUnit(const std::string &name) : _name(name) {}

    /** 调试输出使用的名字 */
    const std::string &name() const { return _name; }

    /**
     * 置 ERR_STATUS 的 bit；ERR_STATUS 原为 0 时锁存这次错误的偏移和
     * 访问信息
     */
    void logError(uint32_t bit, Addr off, bool is_write, unsigned size,
                  unsigned bar);

    /** ERR_STATUS 的当前值 */
    uint32_t errStatus() const { return _errStatus; }

    /** ERR_ADDR：第一次错误的 BAR 偏移 */
    uint32_t errAddr() const { return _errAddr; }

    /** ERR_INFO：第一次错误的访问类型、字节数和 BAR */
    uint32_t errInfo() const { return _errInfo; }

    /** 写 1 清零 ERR_STATUS 中 bits 指定的位 */
    void clearErr(uint32_t bits);

    /** INT_STATUS 的当前值 */
    uint32_t intStatus() const { return _intStatus; }

    /** 写 1 清零 INT_STATUS 中 bits 指定的位；错误仍在时 ERROR 重新置位 */
    void clearInt(uint32_t bits);

    /** 置位 INT_STATUS 中 bits 指定的位（INT_SET） */
    void setInt(uint32_t bits) { _intStatus |= bits; }

    /** INT_MASK 的当前值 */
    uint32_t intMask() const { return _intMask; }

    /** 写 INT_MASK */
    void setIntMask(uint32_t v) { _intMask = v; }

    /** 把全部状态恢复为复位值 */
    void reset();

    /** 保存全部状态 */
    void serialize(CheckpointOut &cp) const override;

    /** 恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp) override;

  private:
    /** 错误条件仍然成立时置 INT_STATUS.ERROR */
    void updateIntStatus();

    /** 调试输出使用的名字 */
    std::string _name;
    /** INT_STATUS */
    uint32_t _intStatus = 0;
    /** INT_MASK */
    uint32_t _intMask = 0xFFFFFFFF;
    /** ERR_STATUS */
    uint32_t _errStatus = 0;
    /** ERR_ADDR：第一次错误的 BAR 偏移 */
    uint32_t _errAddr = 0;
    /** ERR_INFO：第一次错误的访问类型、字节数和 BAR */
    uint32_t _errInfo = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_STATUS_STATUS_UNIT_HH__
