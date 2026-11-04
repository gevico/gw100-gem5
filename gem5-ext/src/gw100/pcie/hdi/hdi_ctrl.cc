/**
 * @file  hdi_ctrl.cc
 * @brief HdiCtrl 的实现
 *
 * @see spec-hdi, spec-pcie
 */

#include "src/gw100/pcie/hdi/hdi_ctrl.hh"

#include <algorithm>

namespace gem5
{
namespace gw100
{

namespace
{

/** HDI_CAP_LO 的复位值：MQES 4095，MAX_CTX 16，TO 4（2 s） */
constexpr uint32_t CapLoReset = 4095u | (16u << 16) | (4u << 24);
/** HDI_CAP_HI 的复位值：NUM_EVENTS 64，CTX_REGIONS 8 */
constexpr uint32_t CapHiReset = 64u | (8u << 8);
/** HDI_VS 的复位值：0.1 */
constexpr uint32_t VsReset = 0x00000100;
/** HDI_CC 的可写位：EN、SHN */
constexpr uint32_t CcMask = 0x1u | (0x3u << 14);
/** HDI_AQA 的可写位：ASQS、ACQS */
constexpr uint32_t AqaMask = 0x0FFFu | (0x0FFFu << 16);

} // anonymous namespace

/** 构造：寄存器为复位值 */
HdiCtrl::HdiCtrl()
{
    reset();
}

/** 复位：寄存器、HDI_WR_MAP、BUSY、doorbell 待发槽恢复复位值 */
void
HdiCtrl::reset()
{
    reg.fill(0);
    reg[0x000 / 4] = CapLoReset;
    reg[0x004 / 4] = CapHiReset;
    reg[0x008 / 4] = VsReset;
    wrMap = 0;
    busyPend = false;
    busyFw = false;
    slotValid.fill(false);
    order.clear();
}

/** host 视角：偏移 off 是否是已定义的寄存器（spec-hdi §4） */
bool
HdiCtrl::defined(Addr off)
{
    switch (off) {
      case 0x000: case 0x004: case 0x008: case 0x010: case 0x014:
      case 0x018: case 0x020: case 0x024: case 0x028: case 0x02C:
      case 0x030: case 0x034:
        return true;
    }
    return false;
}

/** host 读偏移 off 的寄存器 */
uint32_t
HdiCtrl::hostRead(Addr off) const
{
    uint32_t v = reg[off / 4];
    if (off == Csts)
        v = (v & ~CstsBusy) | ((busyPend || busyFw) ? CstsBusy : 0);
    return v;
}

/** 记录一次可写寄存器的 host 写入 */
void
HdiCtrl::recordWrite(Addr off)
{
    wrMap |= 1u << (off / 4);
    if (off == Cc)
        busyPend = true;
    if (onHdiWr)
        onHdiWr();
}

/** host 写偏移 off 的寄存器：只更新可写字段 */
void
HdiCtrl::hostWrite(Addr off, uint32_t v)
{
    if (gated)
        return;  // FLR 门控期间丢弃（spec-pcie §13.2）
    bool en = reg[Cc / 4] & 1;
    switch (off) {
      case Cc:
        reg[Cc / 4] = v & CcMask;
        recordWrite(off);
        return;
      case 0x018:
        if (!en) {
            reg[off / 4] = v & AqaMask;
            recordWrite(off);
        }
        return;
      case 0x020: case 0x028:
        if (!en) {
            reg[off / 4] = v & ~0xFFFu;   // 4 KiB 对齐
            recordWrite(off);
        }
        return;
      case 0x024: case 0x02C:
        if (!en) {
            reg[off / 4] = v;
            recordWrite(off);
        }
        return;
      default:
        return;  // 只读寄存器：写入被忽略
    }
}

/** host 写 doorbell 页 page 内偏移 off（0 为 SQ_TAIL，4 为 CQ_HEAD） */
void
HdiCtrl::hostDoorbell(unsigned page, Addr off, uint32_t v)
{
    if (gated || page >= Pages || (off != 0 && off != 4))
        return;
    unsigned s = 2 * page + off / 4;
    slotVal[s] = uint16_t(v);
    // 覆盖时移到发出顺序的最后（spec-pcie §5.3）
    if (slotValid[s])
        order.erase(std::find(order.begin(), order.end(), s));
    slotValid[s] = true;
    order.push_back(s);
    trySendDoorbells();
}

/** 按顺序发出待发的 doorbell，直到 sendDb 不再接受 */
void
HdiCtrl::trySendDoorbells()
{
    while (!order.empty()) {
        unsigned s = order.front();
        if (!sendDb || !sendDb(s, slotVal[s]))
            return;
        slotValid[s] = false;
        order.pop_front();
    }
}

/** 固件经 HIF 区读偏移 off（0x000 – 0x0FF 与 HDI_WR_MAP）；其余返回 false */
bool
HdiCtrl::fwRead(Addr off, uint32_t &v) const
{
    if (off < 0x100) {
        v = hostRead(off);
        return true;
    }
    if (off == WrMap) {
        v = wrMap;
        return true;
    }
    return false;
}

/** 固件经 HIF 区写偏移 off；其余返回 false */
bool
HdiCtrl::fwWrite(Addr off, uint32_t v)
{
    if (off >= 0x100 && off != WrMap)
        return false;
    if (gated)
        return true;  // FLR 门控期间固件的影子写被忽略
    if (off == WrMap) {
        uint32_t cleared = v & wrMap;
        wrMap &= ~v;
        if (cleared & (1u << (Cc / 4)))
            busyPend = false;
        return true;
    }
    if (!defined(off))
        return true;  // 保留偏移：写入被忽略
    if (off == Csts) {
        busyFw = v & CstsBusy;
        v &= ~CstsBusy;
    }
    reg[off / 4] = v;
    return true;
}

} // namespace gw100
} // namespace gem5
