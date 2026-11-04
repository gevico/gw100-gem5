/**
 * @file  mgmt_tile.cc
 * @brief Gw100MgmtTile 的实现
 *
 * @see spec-mgmt-tile, spec-mgmt-link
 */

#include "src/gw100/mgmt/mgmt_tile.hh"

#include <cstring>

#include "arch/generic/decoder.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "debug/Gw100Mgmt.hh"
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
/** 本地地址：DPA 空间的起始（本地地址 = NiBase + DPA） */
constexpr Addr NiBase = 0x100'0000'0000;

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
    r.push_back(RangeSize(MgmtWdt::Base, MgmtWdt::Size));
    r.push_back(RangeSize(MgmtLocalRegs::Base, MgmtLocalRegs::Size));
    r.push_back(RangeSize(MgmtMailbox::Base, MgmtMailbox::Size));
    r.push_back(RangeSize(MgmtCrc::Base, MgmtCrc::Size));
    r.push_back(RangeSize(HifBase, HifSize));
    if (tile.ni.isConnected())
        r.push_back(RangeSize(NiBase, tile.dpaSize));
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
      crc(MgmtCrc::Config{p.pll_lock, p.crc_access, p.clk_switch_pause},
          curTick()),
      pllPerfLevel(p.pll_perf_level),
      mgmtClk(dynamic_cast<SrcClockDomain *>(p.clk_domain)),
      clkSwitchEvent([this] {
          mgmtClk->perfLevel(pllPerfLevel);
          wdtUpdate();  // 时钟沿变了，重新安排到期事件
      }, name() + ".clkSwitch", false, Event::CPU_Switch_Pri),
      tsPeriod(p.timestamp_freq),
      tsSync(p.ts_sync),
      cpu(p.cpu),
      wdtFatal(name() + ".wdt_fatal", 0, this),
      haltPc(MgmtSram::RomBase + p.rom_size - 4),
      devResetReq(name() + ".dev_reset_req", 0, this),
      pcieReset(name() + ".pcie_reset"),
      devResetSync(p.dev_reset_sync),
      devResetHold(p.dev_reset_hold),
      xtalPerfLevel(p.xtal_perf_level),
      devResetAssertEvent([this] { devResetAssert(); },
                          name() + ".devResetAssert"),
      devResetReleaseEvent([this] { devResetRelease(); },
                           name() + ".devResetRelease"),
      wdtEvent([this] { wdtExpire(); }, name() + ".wdt"),
      pio(name() + ".pio", *this),
      ni(name() + ".ni", *this),
      dpaSize(1ULL << p.device_pa_bits),
      mlEvent([this] { mlFinish(); }, name() + ".mlFinish")
{
    fatal_if(!mgmtClk || !mgmtClk->validPerfLevel(pllPerfLevel) ||
             !mgmtClk->validPerfLevel(xtalPerfLevel),
             "%s: 管理系统的时钟域没有频率档 %u、%u", name(), pllPerfLevel,
             xtalPerfLevel);
    crc.onMgmtSwitch = [this](Tick when) {
        if (!clkSwitchEvent.scheduled())
            schedule(clkSwitchEvent, when);
    };
    fatal_if(!link, "%s: 没有接管理链路", name());
    fatal_if(!plic, "%s: 没有接 PLIC", name());
    fatal_if(cpu && cpu->numThreads != 1, "%s: 管理 CPU 只能有一个线程",
             name());
    if (cpu) {
        intr = dynamic_cast<MgmtInterrupts *>(cpu->getInterruptController(0));
        fatal_if(!intr, "%s: 管理 CPU 的中断控制器必须是 "
                 "Gw100MgmtInterrupts", name());
    }
    mailbox.onChange = [this] { updateIrqs(); };
    regs.onChange = [this] { updateIrqs(); };
}

/** 返回名为 if_name 的端口：pio、ni、wdt_fatal、dev_reset_req、pcie_reset */
Port &
Gw100MgmtTile::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "pio")
        return pio;
    if (if_name == "ni")
        return ni;
    if (if_name == "wdt_fatal") {
        fatal_if(idx > 0, "%s: wdt_fatal 只有一个", name());
        return wdtFatal;
    }
    if (if_name == "dev_reset_req")
        return devResetReq;
    if (if_name == "pcie_reset")
        return pcieReset;
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

/** 时刻 t 的时钟沿上，看门狗看到的同步后的 mtime 样本 */
uint64_t
Gw100MgmtTile::mtimeSample(Tick t) const
{
    Tick lag = cyclesToTicks(tsSync);
    return t > lag ? (t - lag) / tsPeriod : 0;
}

/** 看门狗状态变化后：更新中断，进入第二级，重新安排到期事件 */
void
Gw100MgmtTile::wdtUpdate()
{
    updateIrqs();
    if (wdt.stage2() && !halted)
        haltCpu();
    if (wdtEvent.scheduled())
        deschedule(wdtEvent);
    if (!wdt.counting())
        return;
    // 第一个满足 mtimeSample(e) ≥ T0 + L 的时钟沿 e
    Tick lag = cyclesToTicks(tsSync);
    uint64_t dl = wdt.deadline();
    if (dl > (MaxTick - lag) / tsPeriod)
        return;  // 远在仿真能到达的时刻之后
    Tick target = dl * tsPeriod + lag;
    Tick e = clockEdge();
    if (target > e)
        e = clockEdge(ticksToCycles(target - e));
    schedule(wdtEvent, e);
}

/** 到期事件：在当前时钟沿判定一次自然到期 */
void
Gw100MgmtTile::wdtExpire()
{
    wdt.evaluate(mtimeSample(curTick()));
    wdtUpdate();
}

/** 看门狗第二级超时：置位 wdt_fatal 信号，停住管理 CPU */
void
Gw100MgmtTile::haltCpu()
{
    halted = true;
    inform("%s: 看门狗第二级超时，停住管理 CPU 并置位 DEV_FATAL", name());
    // 专用信号与 CPU 停机互不依赖
    if (wdtFatal.isConnected())
        wdtFatal.raise();
    requestCpuHalt();
}

/** 请求管理 CPU 在下一个指令边界停住（正在 wfi 的 CPU 先唤醒） */
void
Gw100MgmtTile::requestCpuHalt()
{
    if (!intr)
        return;
    intr->requestHalt(haltPc);
    cpu->wakeup(0);
}

/** dev_reset_req 置位：经同步器后开始设备完全复位 */
void
Gw100MgmtTile::raiseInterruptPin(int num)
{
    (void)num;
    if (!inDevReset && !devResetAssertEvent.scheduled())
        schedule(devResetAssertEvent, curTick() + devResetSync);
}

/** dev_reset_req 撤销：PCIe 的功能逻辑复位时撤销，无动作 */
void
Gw100MgmtTile::lowerInterruptPin(int num)
{
    (void)num;
}

/**
 * 设备完全复位开始（spec-clock-reset-power §4.3）：复位 PCIe 的
 * 功能逻辑、ML 的两端和本 Tile，CPU 停住，时钟回到 XTAL
 */
void
Gw100MgmtTile::devResetAssert()
{
    DPRINTF(Gw100Mgmt, "设备完全复位：开始\n");
    inDevReset = true;
    if (pcieReset.isConnected())
        pcieReset.set(true);  // PCIe 的功能逻辑与 ML 的 HIF 侧
    link->reset(Gw100MgmtLink::Side::Mgmt);
    // ML 的复位丢弃了在途的 REG_RD，CPU 等待的读以访问错误结束
    for (PacketPtr pkt : hifReads)
        respondError(pkt);
    hifReads.clear();
    if (mlEvent.scheduled())
        deschedule(mlEvent);
    mlRespond = nullptr;
    // CPU 在下一个指令边界停住，等待复位释放
    restartPending = false;
    requestCpuHalt();
    // 本地寄存器、信箱、看门狗、CRC 恢复复位值，时钟回到 XTAL
    if (clkSwitchEvent.scheduled())
        deschedule(clkSwitchEvent);
    mgmtClk->perfLevel(xtalPerfLevel);
    crc.devReset();
    wdt.reset();
    if (wdtEvent.scheduled())
        deschedule(wdtEvent);
    halted = false;
    if (wdtFatal.isConnected())
        wdtFatal.lower();
    mailbox.reset();
    regs.reset();
    updateIrqs();
    schedule(devResetReleaseEvent, curTick() + devResetHold);
}

/** 设备完全复位释放：SRAM 开始硬件清零，ML 重新建立，CPU 重新开始 */
void
Gw100MgmtTile::devResetRelease()
{
    DPRINTF(Gw100Mgmt, "设备完全复位：释放\n");
    inDevReset = false;
    sram.startInit(curTick(), clockPeriod());
    link->release(Gw100MgmtLink::Side::Mgmt);
    if (pcieReset.isConnected())
        pcieReset.set(false);
    restartPending = true;
    if (heldFetch)
        restartCpu();
}

/**
 * 复位 CPU 的状态，用复位向量处的指令响应扣下的取指，CPU 从启动
 * ROM 重新开始
 */
void
Gw100MgmtTile::restartCpu()
{
    ThreadContext *tc = cpu->getContext(0);
    intr->clearHalt();
    tc->getIsaPtr()->clear();
    tc->getIsaPtr()->resetThread();  // PC 回到复位向量，PMP 复位
    intr->clearAll();
    tc->getDecoderPtr()->reset();
    PacketPtr pkt = heldFetch;
    heldFetch = nullptr;
    restartPending = false;
    Addr pc = tc->pcState().instAddr();
    unsigned sz = pkt->getSize();
    panic_if(pc % sz || !sram.inRom(pc, sz),
             "%s: 复位向量 %#x 不在启动 ROM 中", name(), pc);
    DPRINTF(Gw100Mgmt, "管理 CPU 从 %#x 重新开始\n", pc);
    pkt->setData(sram.ptr(pc));
    respond(pkt, clockEdge(Cycles(1)));
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
    if (a >= MgmtWdt::Base && a < MgmtWdt::Base + MgmtWdt::Size) {
        Addr off = a - MgmtWdt::Base;
        uint64_t v = 0;
        if (wr) {
            if (!wdt.write(off, sz, pkt->getUintX(ByteOrder::little),
                           mtimeSample(now)))
                return MaxTick;
            wdtUpdate();
        } else {
            if (!wdt.read(off, sz, v))
                return MaxTick;
            pkt->setUintX(v, ByteOrder::little);
        }
        return one;
    }
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
    if (a >= MgmtCrc::Base && a < MgmtCrc::Base + MgmtCrc::Size) {
        // CRC 在 clk_ao 域，经请求/应答握手访问（spec-clock-reset-power
        // §6）；只支持 32 位访问
        if (sz != 4)
            return MaxTick;
        Addr off = a - MgmtCrc::Base;
        if (wr)
            return crc.write(off, pkt->getUintX(ByteOrder::little), now);
        uint32_t v = 0;
        if (!crc.read(off, v, now + crc.accessLatency()))
            return MaxTick;
        pkt->setUintX(v, ByteOrder::little);
        return now + crc.accessLatency();
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
    hifReads.insert(pkt);
    t->done = [this, pkt](MlStatus s, const MlData &d) {
        hifReads.erase(pkt);
        if (s != MlStatus::Ok) {
            respondError(pkt);
            return;
        }
        pkt->setUintX(le32(d.data()), ByteOrder::little);
        respond(pkt, clockEdge());
    };
    if (!ml->submit(cpuClient, t)) {
        hifReads.erase(pkt);
        respondError(pkt);
    }
}

/** 管理 CPU 经 NI 访问 DPA 空间（spec-mgmt-tile §3.3） */
void
Gw100MgmtTile::accessNi(PacketPtr pkt)
{
    if (!ni.isConnected()) {
        respondError(pkt);
        return;
    }
    auto req = std::make_shared<Request>(pkt->getAddr() - NiBase,
                                         pkt->getSize(), 0,
                                         pkt->req->requestorId());
    PacketPtr np = new Packet(req, pkt->cmd);
    np->allocate();
    if (pkt->isWrite())
        np->setData(pkt->getConstPtr<uint8_t>());
    auto *st = new NiState;
    st->orig = pkt;
    np->pushSenderState(st);
    ni.send(np);
}

/** NI 请求的响应到达 */
void
Gw100MgmtTile::niResponse(PacketPtr np)
{
    auto *st = dynamic_cast<NiState *>(np->popSenderState());
    panic_if(!st, "%s: NI 响应没有发送状态", name());
    PacketPtr pkt = st->orig;
    delete st;
    if (np->isError()) {
        respondError(pkt);
    } else {
        if (pkt->isRead())
            pkt->setData(np->getConstPtr<uint8_t>());
        respond(pkt, clockEdge());
    }
    delete np;
}

/** 发出一个请求；被反压时留待重试 */
void
Gw100MgmtTile::NiPort::send(PacketPtr pkt)
{
    if (waiting.empty() && sendTimingReq(pkt))
        return;
    waiting.push_back(pkt);
}

/** 请求的响应：交回管理 Tile */
bool
Gw100MgmtTile::NiPort::recvTimingResp(PacketPtr pkt)
{
    tile.niResponse(pkt);
    return true;
}

/** 可以重试被反压的请求 */
void
Gw100MgmtTile::NiPort::recvReqRetry()
{
    while (!waiting.empty() && sendTimingReq(waiting.front()))
        waiting.pop_front();
}

/** 处理一次时序访问，在完成时刻经端口返回响应 */
void
Gw100MgmtTile::handleTiming(PacketPtr pkt)
{
    if (pkt->req->isInstFetch() && intr && intr->haltTaken() &&
        pkt->getAddr() == haltPc) {
        // CPU 已在指令边界停住：扣下这次取指
        heldFetch = pkt;
        if (restartPending)
            restartCpu();
        return;
    }
    Addr a = pkt->getAddr();
    if (a >= HifBase && a < HifBase + HifSize) {
        accessHif(pkt);
        return;
    }
    if (a >= NiBase && a - NiBase < dpaSize) {
        accessNi(pkt);
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
    lv[IrqWdt] = wdt.stage1();
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
