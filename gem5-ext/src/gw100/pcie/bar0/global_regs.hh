/**
 * @file  global_regs.hh
 * @brief BAR0 的设备标识与全局控制寄存器块（0x0000）
 *
 * 设备标识、SCRATCH、时间戳在本块保存；INT_* 与 ERR_* 只是 StatusUnit
 * 的视图。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_BAR0_GLOBAL_REGS_HH__
#define __GW100_PCIE_BAR0_GLOBAL_REGS_HH__

#include <cstdint>

#include "base/types.hh"
#include "dev/reg_bank.hh"
#include "src/gw100/pcie/pcie_types.hh"
#include "src/gw100/pcie/status/status_unit.hh"

namespace gem5
{
namespace gw100
{

/** 设备标识与全局控制寄存器（spec-pcie §6.1） */
class GlobalRegs : public RegBlock
{
  public:
    /** 本块在 BAR0 中的起始偏移 */
    static constexpr Addr Base = 0x0000;

    /**
     * 构造；version、dev_caps 是 DEV_VERSION、DEV_CAPS 的值，
     * link_speed、link_width 写入 DEV_STATUS，timestamp_hz 是时间戳频率
     */
    GlobalRegs(StatusUnit &status, uint32_t version, uint32_t dev_caps,
               uint8_t link_speed, uint8_t link_width,
               uint32_t timestamp_hz);

    /** 本块在 BAR0 中的起始偏移 */
    Addr base() const override { return Base; }

    /** BAR0 偏移 off 处的 32 位字是否是本块已实现的寄存器 */
    bool implemented(Addr off) const override;

    /** 读 BAR0 偏移 off 起的 size 字节到 data；调用者已检查宽度和偏移 */
    void read(Addr off, uint8_t *data, unsigned size) override
    { bank.read(off, data, size); }

    /** 把 data 中的 size 字节写到 BAR0 偏移 off 起；调用者已检查 */
    void write(Addr off, const uint8_t *data, unsigned size) override
    { bank.write(off, data, size); }

    /** 把 SCRATCH 和时间戳锁存值恢复为复位值；INT/ERR 由 StatusUnit 复位 */
    void reset() override;

    /** 保存 SCRATCH 和时间戳锁存值 */
    void serialize(CheckpointOut &cp) const override;

    /** 恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp) override;

  private:
    /** 当前设备时间戳：仿真时间换算到 timestampHz */
    uint64_t timestamp() const;

    /** INT_* 与 ERR_* 寄存器背后的状态 */
    StatusUnit &status;
    /** DEV_VERSION 的值 */
    const uint32_t version;
    /** DEV_CAPS 的值：窗口数、MSI-X 表项数、上下文数、DMA 通道数 */
    const uint32_t devCaps;
    /** 当前链路速率：1 = Gen1 … 4 = Gen4 */
    const uint8_t linkSpeed;
    /** 当前链路宽度 */
    const uint8_t linkWidth;
    /** 设备时间戳频率，Hz */
    const uint32_t timestampHz;

    /** SCRATCH */
    uint32_t scratch = 0;
    /** 读 TIMESTAMP_LO 时锁存的高 32 位，供 TIMESTAMP_HI 返回 */
    uint32_t tsLatchedHi = 0;

    /** 本块的寄存器组；寄存器对象只是上面状态的视图 */
    RegisterBankLE bank{"global", Base};
    /** DEV_VERSION（0x000，RO） */
    RegisterBankLE::Register32 regDevVersion{"DEV_VERSION"};
    /** DEV_CAPS（0x004，RO） */
    RegisterBankLE::Register32 regDevCaps{"DEV_CAPS"};
    /** DEV_STATUS（0x008，RO） */
    RegisterBankLE::Register32 regDevStatus{"DEV_STATUS"};
    /** SCRATCH（0x00C，RW） */
    RegisterBankLE::Register32 regScratch{"SCRATCH"};
    /** TIMESTAMP_LO（0x010，RO，读取时锁存高位） */
    RegisterBankLE::Register32 regTsLo{"TIMESTAMP_LO"};
    /** TIMESTAMP_HI（0x014，RO，返回锁存值） */
    RegisterBankLE::Register32 regTsHi{"TIMESTAMP_HI"};
    /** TIMESTAMP_FREQ（0x018，RO） */
    RegisterBankLE::Register32 regTsFreq{"TIMESTAMP_FREQ"};
    /** INT_STATUS（0x020，RW1C） */
    RegisterBankLE::Register32 regIntStatus{"INT_STATUS"};
    /** INT_MASK（0x024，RW） */
    RegisterBankLE::Register32 regIntMask{"INT_MASK"};
    /** INT_SET（0x028，W1S） */
    RegisterBankLE::Register32 regIntSet{"INT_SET"};
    /** ERR_STATUS（0x030，RW1C） */
    RegisterBankLE::Register32 regErrStatus{"ERR_STATUS"};
    /** ERR_ADDR（0x034，RO） */
    RegisterBankLE::Register32 regErrAddr{"ERR_ADDR"};
    /** ERR_INFO（0x038，RO） */
    RegisterBankLE::Register32 regErrInfo{"ERR_INFO"};
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR0_GLOBAL_REGS_HH__
