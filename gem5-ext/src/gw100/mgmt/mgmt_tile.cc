/**
 * @file  mgmt_tile.cc
 * @brief Gw100MgmtTile 的实现
 *
 * @see spec-mgmt-tile, spec-mgmt-link
 */

#include "src/gw100/mgmt/mgmt_tile.hh"

#include <cstring>

#include "base/logging.hh"
#include "mem/packet_access.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 本地地址：HIF 寄存器区的起始（spec-mgmt-tile §3.3） */
constexpr Addr HifBase = 0x3000'0000;
/** HIF 寄存器区的大小 */
constexpr Addr HifSize = 0x1000'0000;
/** 本地地址：CRC 寄存器的起始 */
constexpr Addr CrcBase = 0x2002'0000;
/** CRC 寄存器区的大小 */
constexpr Addr CrcSize = 0x1000;

/** 按小端序读取 data 起的 4 字节 */
uint32_t
le32(const uint8_t *data)
{
    return data[0] | (data[1] << 8) | (data[2] << 16) |
           (uint32_t(data[3]) << 24);
}

} // anonymous namespace

/** 构造：tile 为所属的管理 Tile */
Gw100MgmtTile::PioPort::PioPort(const std::string &name,
                                Gw100MgmtTile &tile)
    : QueuedResponsePort(name, queue), tile(tile), queue(tile, *this)
{
}

/** 时序访问：交给管理 Tile，稍后经队列返回响应 */
bool
Gw100MgmtTile::PioPort::recvTimingReq(PacketPtr pkt)
{
    tile.handleTiming(pkt);
    return true;
}

/** 原子访问：只支持 ROM、SRAM 和本地寄存器 */
Tick
Gw100MgmtTile::PioPort::recvAtomic(PacketPtr pkt)
{
    Tick now = tile.clockEdge();
    Tick done = tile.accessLocal(pkt, now);
    if (done == MaxTick) {
        pkt->makeResponse();
        pkt->setBadAddress();
        return tile.clockPeriod();
    }
    pkt->makeResponse();
    return done - curTick();
}

/** 功能访问：用于加载 ROM 镜像和调试读取 */
void
Gw100MgmtTile::PioPort::recvFunctional(PacketPtr pkt)
{
    Addr a = pkt->getAddr();
    unsigned sz = pkt->getSize();
    if (tile.sram.inRom(a, sz) || tile.sram.inSram(a, sz)) {
        if (pkt->isWrite())
            pkt->writeData(tile.sram.ptr(a));
        else
            pkt->setData(tile.sram.ptr(a));
    } else if (pkt->isRead()) {
        // 寄存器的功能读只用于调试，返回 0，不产生副作用
        std::memset(pkt->getPtr<uint8_t>(), 0, sz);
    }
    pkt->makeResponse();
}

/** 本端口响应的地址范围 */
AddrRangeList
Gw100MgmtTile::PioPort::getAddrRanges() const
{
    AddrRangeList r;
    r.push_back(RangeSize(MgmtSram::RomBase, tile.sram.romSize));
    r.push_back(RangeSize(MgmtSram::SramBase, tile.sram.sramSize));
    r.push_back(RangeSize(MgmtLocalRegs::Base, MgmtLocalRegs::Size));
    r.push_back(RangeSize(MgmtMailbox::Base, MgmtMailbox::Size));
    r.push_back(RangeSize(CrcBase, CrcSize));
    r.push_back(RangeSize(HifBase, HifSize));
    return r;
}

/** 按参数构造各组件 */
Gw100MgmtTile::Gw100MgmtTile(const Params &p)
    : ClockedObject(p),
      sram(p.rom_size, p.sram_size, p.sram_banks),
      mailbox(p.db_slots),
      regs(sram),
      fwLoadMax(p.fw_load_max),
      link(p.mgmt_link),
      plic(p.plic),
      pio(name() + ".pio", *this),
      mlEvent([this] { mlFinish(); }, name() + ".mlFinish")
{
    fatal_if(!link, "%s: 没有接管理链路", name());
    fatal_if(!plic, "%s: 没有接 PLIC", name());
    mailbox.onChange = [this] { updateIrqs(); };
    regs.onChange = [this] { updateIrqs(); };
}

/** 返回名为 if_name 的端口：pio */
Port &
Gw100MgmtTile::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "pio")
        return pio;
    return ClockedObject::getPort(if_name, idx);
}

/** 接入 ML 的管理侧，开始 SRAM 的硬件清零 */
void
Gw100MgmtTile::init()
{
    ClockedObject::init();
    fatal_if(!pio.isConnected(), "%s: pio 端口没有连接", name());
    pio.sendRangeChange();
    ml = &link->attach(Gw100MgmtLink::Side::Mgmt, *this, *this);
    ml->onChange = [this] { updateIrqs(); };
    cpuClient = ml->addClient();
    regs.bindMl(ml);
    sram.startInit(curTick(), clockPeriod());
}

/** 撤销 ML 管理侧的复位 */
void
Gw100MgmtTile::startup()
{
    ClockedObject::startup();
    link->release(Gw100MgmtLink::Side::Mgmt);
}

/** 在 when 时刻返回 pkt 的响应 */
void
Gw100MgmtTile::respond(PacketPtr pkt, Tick when)
{
    pkt->makeResponse();
    pio.schedTimingResp(pkt, when);
}

/** 把 pkt 标记为访问错误并在下一个时钟沿返回 */
void
Gw100MgmtTile::respondError(PacketPtr pkt)
{
    pkt->makeResponse();
    pkt->setBadAddress();
    pio.schedTimingResp(pkt, clockEdge(Cycles(1)));
}

/**
 * 访问 ROM、SRAM、本地寄存器或信箱，返回完成时刻；不属于这些区域
 * 或访问非法时返回 MaxTick，并在 pkt 上标记错误
 */
Tick
Gw100MgmtTile::accessLocal(PacketPtr pkt, Tick now)
{
    Addr a = pkt->getAddr();
    unsigned sz = pkt->getSize();
    bool wr = pkt->isWrite();
    Tick per = clockPeriod();
    Tick one = now + per;

    if (sram.inRom(a, sz)) {
        if (wr)
            return MaxTick;  // ROM 只读
        pkt->setData(sram.ptr(a));
        return one;
    }
    if (sram.inSram(a, sz)) {
        Tick done = sram.schedule(a, sz, wr, now, per);
        if (wr)
            pkt->writeData(sram.ptr(a));
        else
            pkt->setData(sram.ptr(a));
        return done;
    }
    if (sz != 4 && sz != 8)
        return MaxTick;
    if (a >= MgmtLocalRegs::Base && a < MgmtLocalRegs::Base +
        MgmtLocalRegs::Size) {
        uint64_t v = 0;
        if (wr) {
            if (!regs.write(a, sz, pkt->getUintX(ByteOrder::little)))
                return MaxTick;
        } else {
            if (!regs.read(a, sz, v, curTick()))
                return MaxTick;
            pkt->setUintX(v, ByteOrder::little);
        }
        return one;
    }
    if (a >= MgmtMailbox::Base && a < MgmtMailbox::Base +
        MgmtMailbox::Size) {
        Addr off = a - MgmtMailbox::Base;
        uint32_t v = 0;
        if (wr) {
            if (!mailbox.write(off, sz, pkt->getUintX(ByteOrder::little)))
                return MaxTick;
        } else {
            if (!mailbox.read(off, sz, v))
                return MaxTick;
            pkt->setUintX(v, ByteOrder::little);
        }
        return one;
    }
    if (a == CrcBase && sz == 4) {
        // CRC_RESET_CAUSE（RW1C）
        if (wr)
            crcResetCause &= ~uint32_t(pkt->getUintX(ByteOrder::little));
        else
            pkt->setUintX(crcResetCause, ByteOrder::little);
        return one;
    }
    return MaxTick;
}

/** 管理 CPU 经 ML 访问 HIF 寄存器区（spec-mgmt-tile §5.2） */
void
Gw100MgmtTile::accessHif(PacketPtr pkt)
{
    Addr off = pkt->getAddr() - HifBase;
    if (pkt->getSize() != 4 || off % 4 || !ml ||
        ml->state() != MlLinkState::Up) {
        respondError(pkt);
        return;
    }
    auto t = std::make_shared<MlTxn>();
    t->addr = off;
    t->len = 4;
    if (pkt->isWrite()) {
        // REG_WR 是 posted 写：进入 ML 后即完成
        t->type = MlType::RegWr;
        t->be = 0xF;
        uint32_t v = pkt->getUintX(ByteOrder::little);
        std::memcpy(t->data.data(), &v, 4);
        if (!ml->submit(cpuClient, t)) {
            respondError(pkt);
            return;
        }
        respond(pkt, clockEdge(Cycles(1)));
        return;
    }
    t->type = MlType::RegRd;
    t->done = [this, pkt](MlStatus s, const MlData &d) {
        if (s != MlStatus::Ok) {
            respondError(pkt);
            return;
        }
        pkt->setUintX(le32(d.data()), ByteOrder::little);
        respond(pkt, clockEdge());
    };
    if (!ml->submit(cpuClient, t))
        respondError(pkt);
}

/** 处理一次时序访问，在完成时刻经端口返回响应 */
void
Gw100MgmtTile::handleTiming(PacketPtr pkt)
{
    Addr a = pkt->getAddr();
    if (a >= HifBase && a < HifBase + HifSize) {
        accessHif(pkt);
        return;
    }
    Tick done = accessLocal(pkt, clockEdge());
    if (done == MaxTick) {
        respondError(pkt);
        return;
    }
    respond(pkt, done);
}

/** ML 管理侧的目标：处理 HIF 发来的请求（spec-mgmt-tile §5.1） */
void
Gw100MgmtTile::mlRequest(const MlTxnPtr &txn, MlRespond respond_fn)
{
    panic_if(mlEvent.scheduled(), "%s: ML 一次只交出一个事务", name());
    Tick now = clockEdge();
    Tick per = clockPeriod();
    Tick done = now + per;
    MlStatus st = MlStatus::Ok;
    MlData data{};

    switch (txn->type) {
      case MlType::BootWr: {
        if (txn->addr + txn->len > fwLoadMax) {
            regs.recordViol(MgmtLocalRegs::ViolBoot,
                            MgmtSram::SramBase + txn->addr);
            st = MlStatus::Reject;
            break;
        }
        Addr a = MgmtSram::SramBase + txn->addr;
        done = sram.schedule(a, txn->len, true, now, per);
        std::memcpy(sram.ptr(a), txn->data.data(), txn->len);
        break;
      }
      case MlType::DbWr: {
        uint16_t v = txn->data[0] | (txn->data[1] << 8);
        if (!mailbox.dbWrite(txn->addr, v))
            st = MlStatus::Reject;
        break;
      }
      case MlType::Event:
      case MlType::Barrier: {
        uint32_t m = le32(txn->data.data());
        if (m & ~0xFFu)
            st = MlStatus::Reject;
        else
            mailbox.eventSet(m);
        break;
      }
      case MlType::DescRd:
      case MlType::DescWr: {
        Addr a = txn->addr;
        if (!sram.inSram(a, txn->len) || !regs.descAllowed(a, txn->len)) {
            st = MlStatus::Reject;
            break;
        }
        bool wr = txn->type == MlType::DescWr;
        done = sram.schedule(a, txn->len, wr, now, per);
        uint8_t *p = sram.ptr(a);
        for (unsigned i = 0; i < txn->len; i++) {
            if (!wr)
                data[i] = p[i];
            else if (txn->be & (1ULL << i))
                p[i] = txn->data[i];
        }
        break;
      }
      default:
        st = MlStatus::Reject;
        break;
    }
    mlRespond = respond_fn;
    mlStatus = st;
    mlData = data;
    schedule(mlEvent, done);
}

/** ML 请求处理完毕的时刻到达：调用保存的 respond */
void
Gw100MgmtTile::mlFinish()
{
    MlRespond r = std::move(mlRespond);
    mlRespond = nullptr;
    r(mlStatus, mlData);
}

/** 按各组件的状态更新 PLIC 中断源的电平 */
void
Gw100MgmtTile::updateIrqs()
{
    std::array<bool, IrqCount> lv{};
    lv[IrqDb] = mailbox.dbIrq();
    lv[IrqEvt] = mailbox.evtIrq();
    lv[IrqMl] = regs.mlIrq();
    lv[IrqCe] = regs.ceIrq();
    lv[IrqUe] = regs.ueIrq();
    lv[IrqAcl] = regs.aclIrq();
    for (unsigned s = 1; s < IrqCount; s++) {
        if (lv[s] == irqLevel[s])
            continue;
        irqLevel[s] = lv[s];
        if (lv[s])
            plic->post(s);
        else
            plic->clear(s);
    }
}

} // namespace gw100
} // namespace gem5
