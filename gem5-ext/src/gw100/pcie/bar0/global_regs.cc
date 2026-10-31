/**
 * @file  global_regs.cc
 * @brief GlobalRegs 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/bar0/global_regs.hh"

#include "sim/core.hh"
#include "sim/cur_tick.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 寄存器回调的参数类型 */
using Reg = RegisterBankLE::Register32;

/** 本块中已定义的偏移（spec-pcie §6.1） */
constexpr Addr Offsets[] = {
    0x000, 0x004, 0x008, 0x00C, 0x010, 0x014, 0x018,
    0x020, 0x024, 0x028, 0x030, 0x034, 0x038,
};

/** DEV_STATUS 的 DEBUG_WIN 位：固件已开启 BAR2 调试窗口 */
constexpr uint32_t DevStatusDebugWin = 1u << 17;

} // anonymous namespace

/**
 * 构造；version、dev_caps 是 DEV_VERSION、DEV_CAPS 的值，
 * link_speed、link_width 写入 DEV_STATUS，timestamp_hz 是时间戳频率
 */
GlobalRegs::GlobalRegs(StatusUnit &status, uint32_t version,
                       uint32_t dev_caps, uint8_t link_speed,
                       uint8_t link_width, uint32_t timestamp_hz)
    : status(status), version(version), devCaps(dev_caps),
      linkSpeed(link_speed), linkWidth(link_width),
      timestampHz(timestamp_hz)
{
    regDevVersion.reader([this](Reg &) { return this->version; })
        .readonly();
    regDevCaps.reader([this](Reg &) { return devCaps; }).readonly();
    regDevStatus.reader([this](Reg &) -> uint32_t {
        // 桩模型相当于固件已在运行并开启了调试窗口，BAR2 窗口始终有效
        return (linkSpeed & 0xF) | (uint32_t(linkWidth & 0x1F) << 4) |
               DevStatusDebugWin;
    }).readonly();
    regScratch.reader([this](Reg &) { return scratch; })
        .writer([this](Reg &, const uint32_t &v) { scratch = v; });
    regTsLo.reader([this](Reg &) -> uint32_t {
        uint64_t t = timestamp();
        tsLatchedHi = uint32_t(t >> 32);
        return uint32_t(t);
    }).readonly();
    regTsHi.reader([this](Reg &) { return tsLatchedHi; }).readonly();
    regTsFreq.reader([this](Reg &) { return timestampHz; }).readonly();
    regIntStatus.reader([this](Reg &) { return this->status.intStatus(); })
        .writer([this](Reg &, const uint32_t &v) {
            this->status.clearInt(v);
        });
    regIntMask.reader([this](Reg &) { return this->status.intMask(); })
        .writer([this](Reg &, const uint32_t &v) {
            this->status.setIntMask(v);
        });
    regIntSet.reader([](Reg &) -> uint32_t { return 0; })
        .writer([this](Reg &, const uint32_t &v) {
            this->status.setInt(v);
        });
    regErrStatus.reader([this](Reg &) { return this->status.errStatus(); })
        .writer([this](Reg &, const uint32_t &v) {
            this->status.clearErr(v);
        });
    regErrAddr.reader([this](Reg &) { return this->status.errAddr(); })
        .readonly();
    regErrInfo.reader([this](Reg &) { return this->status.errInfo(); })
        .readonly();

    bank.addRegistersAt<RegisterBankLE::RegisterRaz>({
        {0x000, regDevVersion}, {0x004, regDevCaps}, {0x008, regDevStatus},
        {0x00C, regScratch}, {0x010, regTsLo}, {0x014, regTsHi},
        {0x018, regTsFreq}, {0x020, regIntStatus}, {0x024, regIntMask},
        {0x028, regIntSet}, {0x030, regErrStatus}, {0x034, regErrAddr},
        {0x038, regErrInfo},
    });
}

/** BAR0 偏移 off 处的 32 位字是否是本块已实现的寄存器 */
bool
GlobalRegs::implemented(Addr off) const
{
    for (Addr a : Offsets) {
        if (a == off)
            return true;
    }
    return false;
}

/** 把 SCRATCH 和时间戳锁存值恢复为复位值；INT/ERR 由 StatusUnit 复位 */
void
GlobalRegs::reset()
{
    scratch = 0;
    tsLatchedHi = 0;
}

/** 当前设备时间戳：仿真时间换算到 timestampHz */
uint64_t
GlobalRegs::timestamp() const
{
    unsigned __int128 t = curTick();
    return uint64_t(t * timestampHz / sim_clock::Frequency);
}

/** 保存 SCRATCH 和时间戳锁存值 */
void
GlobalRegs::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(scratch);
    SERIALIZE_SCALAR(tsLatchedHi);
}

/** 恢复 serialize() 保存的状态 */
void
GlobalRegs::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(scratch);
    UNSERIALIZE_SCALAR(tsLatchedHi);
}

} // namespace gw100
} // namespace gem5
