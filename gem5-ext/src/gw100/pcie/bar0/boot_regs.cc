/**
 * @file  boot_regs.cc
 * @brief BootRegs 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/bar0/boot_regs.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 寄存器回调的参数类型 */
using Reg = RegisterBankLE::Register32;

/** 本块中已定义的偏移（spec-pcie §6.4） */
constexpr Addr Offsets[] = {
    0x4000, 0x4004, 0x4010, 0x4014, 0x4018,
};

} // anonymous namespace

/** 构造并登记各寄存器；ctrl 提供启动状态，fw_load_max 为装载区大小 */
BootRegs::BootRegs(BootCtrl &ctrl, uint32_t fw_load_max)
    : ctrl(ctrl), fwLoadMax(fw_load_max)
{
    regBootStatus.reader([this](Reg &) { return this->ctrl.hostBootStatus(); })
        .readonly();
    regBootCtrl.reader([](Reg &) -> uint32_t { return 0; })
        .writer([this](Reg &, const uint32_t &v) {
            this->ctrl.hostBootCtrl(v);
        });
    regFwLoadMax.reader([this](Reg &) { return fwLoadMax; }).readonly();
    regFwSize.reader([this](Reg &) { return this->ctrl.fwSize(); })
        .writer([this](Reg &, const uint32_t &v) {
            this->ctrl.hostWriteFwSize(v);
        });
    regFwVersion.reader([this](Reg &) { return fwVer; }).readonly();

    bank.addRegistersAt<RegisterBankLE::RegisterRaz>({
        {0x4000, regBootStatus}, {0x4004, regBootCtrl},
        {0x4010, regFwLoadMax}, {0x4014, regFwSize}, {0x4018, regFwVersion},
    });
}

/** BAR0 偏移 off 处的 32 位字是否是本块已实现的寄存器 */
bool
BootRegs::implemented(Addr off) const
{
    for (Addr a : Offsets) {
        if (a == off)
            return true;
    }
    return false;
}

/** 保存 FW_VERSION */
void
BootRegs::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(fwVer);
}

/** 恢复 serialize() 保存的状态 */
void
BootRegs::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(fwVer);
}

} // namespace gw100
} // namespace gem5
