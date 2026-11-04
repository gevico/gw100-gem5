/**
 * @file  boot_ctrl.cc
 * @brief BootCtrl 的实现
 *
 * @see spec-pcie, spec-mgmt-link, spec-mgmt-tile
 */

#include "src/gw100/pcie/boot/boot_ctrl.hh"

#include <algorithm>
#include <cstring>

#include "base/trace.hh"
#include "debug/Gw100Boot.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 事件位：FW_START（spec-mgmt-tile §6.3） */
constexpr uint32_t EvtFwStart = 1u << 0;
/** 事件位：FLR */
constexpr uint32_t EvtFlr = 1u << 1;

/** 构造一个 4 字节的事件事务 */
MlTxnPtr
eventTxn(MlType type, uint32_t mask)
{
    auto t = std::make_shared<MlTxn>();
    t->type = type;
    t->len = 4;
    t->be = 0xF;
    std::memcpy(t->data.data(), &mask, 4);
    return t;
}

} // anonymous namespace

/**
 * 构造：status 记录 BAR2 错误，fw_load_max 为装载区大小，
 * flr_timeout 为 pcie.flr_proc_timeout，em 用于调度处理超时
 */
BootCtrl::BootCtrl(const std::string &name, StatusUnit &status,
                   uint32_t fw_load_max, Tick flr_timeout,
                   EventManager &em)
    : _name(name), status(status), fwLoadMax(fw_load_max),
      flrTimeoutTicks(flr_timeout), em(em),
      flrTimer([this] { flrTimeout(); }, name + ".flrTimer"),
      flrRemaining(flr_timeout)
{
}

/** 接入 ML 的 HIF 侧适配器，登记启动写入和事件两个客户端 */
void
BootCtrl::bindMl(MlEndpoint *ep)
{
    ml = ep;
    bootClient = ml->addClient();
    evtClient = ml->addClient();
}

/** 启动窗口是否打开 */
bool
BootCtrl::windowOpen() const
{
    return stage == 2 && !flrInProgress && !linkFault;
}

/** 窗口状态可能变化后调用：窗口由关闭变为打开时清零 WIN_WRITE */
void
BootCtrl::refreshWindow()
{
    bool open = windowOpen();
    if (open && !lastWindowOpen)
        winWrite = false;
    if (open != lastWindowOpen)
        DPRINTF(Gw100Boot, "启动窗口%s\n", open ? "打开" : "关闭");
    lastWindowOpen = open;
}

/** host 读 BOOT_STATUS：FLR 进行中时按门控显示 */
uint32_t
BootCtrl::hostBootStatus() const
{
    uint32_t st = stage;
    bool ready = fwReady;
    if (flrInProgress) {
        ready = false;
        if (st >= 1 && st <= 3)
            st = 1;
    }
    return st | (ready ? 1u << 8 : 0) | (error << 16);
}

/** host 写 BOOT_CTRL（W1S）：[0] FW_START，[1] DEV_RESET */
void
BootCtrl::hostBootCtrl(uint32_t v)
{
    if ((v & 1) && windowOpen()) {
        DPRINTF(Gw100Boot, "接受 FW_START，FW_SIZE = %d，待提交 %d\n",
                size, commitCount);
        stage = 3;
        error = 0;
        fwStartPending = true;
        refreshWindow();
        trySend();
    }
    if ((v & 2) && onDevReset) {
        DPRINTF(Gw100Boot, "接受 DEV_RESET\n");
        onDevReset();
    }
}

/** host 写 FW_SIZE：只在启动窗口打开时有效 */
void
BootCtrl::hostWriteFwSize(uint32_t v)
{
    if (windowOpen())
        size = v;
}

/**
 * BAR2 在启动窗口模式下的写：off 为窗口内偏移。返回 false 表示
 * 写入非法（已记录 BOOT_WIN 错误与 WIN_WRITE）
 */
bool
BootCtrl::windowWrite(Addr off, unsigned len, const uint8_t *data)
{
    if (off % 8 || len == 0 || len % 8 || off + len > fwLoadMax) {
        status.logError(StatusUnit::ErrBootWin, off, true, len, 2);
        winWrite = true;
        return false;
    }
    // 按 64 B 边界拆成 BOOT_WR，入口处立即计入提交计数
    for (Addr a = off; a < off + len;) {
        unsigned n = std::min<Addr>(64 - a % 64, off + len - a);
        auto t = std::make_shared<MlTxn>();
        t->type = MlType::BootWr;
        t->addr = a;
        t->len = n;
        t->be = n == 64 ? ~0ULL : ((1ULL << n) - 1);
        std::memcpy(t->data.data(), data + (a - off), n);
        t->done = [this](MlStatus st, const MlData &) {
            if (st != MlStatus::Ok) {
                latchLinkFault();
                return;
            }
            commitCount--;
            trySend();
        };
        commitCount++;
        if (!ml || !ml->submit(bootClient, t)) {
            commitCount--;
            latchLinkFault();
        }
        a += n;
    }
    return true;
}

/** 固件读 FW_BOOT_STATUS：HIF 中的实际值 */
uint32_t
BootCtrl::fwBootStatus() const
{
    return stage | (fwReady ? 1u << 8 : 0) | (error << 16);
}

/** 固件写 FW_BOOT_STATUS */
void
BootCtrl::fwWriteBootStatus(uint32_t v)
{
    if (linkFault)
        return;  // 锁存期间固件的写入无效
    stage = v & 0xF;
    fwReady = (v >> 8) & 1;
    if (flrInProgress)
        fwReady = false;  // FLR 进行中写 1 被忽略
    error = (v >> 16) & 0xFF;
    DPRINTF(Gw100Boot, "固件写 STAGE = %d，FW_READY = %d，ERROR = %d\n",
            stage, fwReady, error);
    refreshWindow();
}

/** 固件写 FW_SIZE_FW：只能写 0 */
void
BootCtrl::fwWriteFwSize(uint32_t v)
{
    if (v == 0)
        size = 0;
}

/** 固件读 FLR_STATUS：[0] IN_PROGRESS，[1] DELIVERED，[2] FAIL */
uint32_t
BootCtrl::flrStatus() const
{
    return (flrInProgress ? 1u : 0) | (delivered ? 2u : 0) |
           (flrFailed ? 4u : 0);
}

/** 固件写 FLR_CTRL（W1S）：[0] TAKE，[1] DONE */
void
BootCtrl::flrCtrl(uint32_t v)
{
    if (v & 1)
        delivered = false;
    if ((v & 2) && !flrProcessing() && !delivered && !flrFailed) {
        DPRINTF(Gw100Boot, "FLR 确认，解除门控\n");
        flrInProgress = false;
        refreshWindow();
        if (onFlrDone)
            onFlrDone();
    }
}

/** 发生 FLR（含 D3hot→D0）：在 host 可见状态复位之后调用 */
void
BootCtrl::flr()
{
    DPRINTF(Gw100Boot, "FLR：门控，待提交 %d\n", commitCount);
    fwReady = false;
    size = 0;
    winWrite = false;
    debugWin = false;  // FLR 关闭调试窗口（spec-pcie §7.3）
    flrInProgress = true;
    // FW_START 已被接受、事件尚未被 ML 接受：取消
    fwStartPending = false;
    pendingEvt = 0;
    // 上一次的屏障尚未被 ML 接受时与本次合并
    flrNeedSend = true;
    if (!flrFailed) {
        if (flrTimer.scheduled())
            em.deschedule(flrTimer);
        flrRemaining = flrTimeoutTicks;
        updateFlrTimer();
    }
    refreshWindow();
    trySend();
}

/**
 * 设备完全复位：全部状态恢复复位值，包括 DEV_FATAL 和启动通路故障
 * 的锁存（spec-pcie §13.1）；ML 由外壳另行复位
 */
void
BootCtrl::devReset()
{
    DPRINTF(Gw100Boot, "设备完全复位\n");
    debugWin = false;
    fatal = false;
    status.setFatal(false);
    stage = 0;
    fwReady = false;
    error = 0;
    size = 0;
    winWrite = false;
    lastWindowOpen = false;
    linkFault = false;
    // ML 的复位丢弃了在途的启动写入和事件，不会再有确认
    commitCount = 0;
    fwStartPending = false;
    flrInProgress = false;
    flrNeedSend = false;
    barriersOut = 0;
    delivered = false;
    flrFailed = false;
    pendingEvt = 0;
    if (flrTimer.scheduled())
        em.deschedule(flrTimer);
    flrRemaining = flrTimeoutTicks;
    flrTimerStart = 0;
}

/**
 * 向管理 Tile 发出 HIF 的事件（BME_OFF 等，spec-mgmt-tile §6.3）；
 * 链路未建立时保留，建立后发出
 */
void
BootCtrl::hostEvent(uint32_t bits)
{
    pendingEvt |= bits;
    trySend();
}

/** 尝试发出待发的 FW_START 事件和 FLR 屏障 */
void
BootCtrl::trySend()
{
    if (!ml || ml->state() != MlLinkState::Up || commitCount != 0)
        return;
    if (fwStartPending) {
        if (ml->submit(evtClient, eventTxn(MlType::Event, EvtFwStart))) {
            DPRINTF(Gw100Boot, "发出 FW_START 事件\n");
            fwStartPending = false;
        }
    }
    if (flrNeedSend) {
        auto t = eventTxn(MlType::Barrier, EvtFlr);
        t->done = [this](MlStatus st, const MlData &) {
            if (st != MlStatus::Ok) {
                flrFail();
                return;
            }
            barriersOut--;
            delivered = true;
            DPRINTF(Gw100Boot, "FLR 事件已送达\n");
            updateFlrTimer();
        };
        if (ml->submit(evtClient, t)) {
            flrNeedSend = false;
            barriersOut++;
        }
    }
    if (pendingEvt && !flrNeedSend &&
        ml->submit(evtClient, eventTxn(MlType::Event, pendingEvt))) {
        DPRINTF(Gw100Boot, "发出 HIF 事件 %#x\n", pendingEvt);
        pendingEvt = 0;
    }
}

/** 锁存启动通路故障（spec-pcie §7.2） */
void
BootCtrl::latchLinkFault()
{
    if (linkFault)
        return;
    DPRINTF(Gw100Boot, "锁存启动通路故障\n");
    linkFault = true;
    stage = 0xF;
    error = ErrLink;
    fwReady = false;
    fwStartPending = false;
    setDevFatal();
    refreshWindow();
}

/**
 * 置位 DEV_FATAL：硬件故障、固件经 DEV_CTRL_FW 请求，或看门狗第二级
 * 超时的专用信号（spec-mgmt-tile §7.4）
 */
void
BootCtrl::setDevFatal()
{
    if (fatal)
        return;
    DPRINTF(Gw100Boot, "置位 DEV_FATAL\n");
    fatal = true;
    status.setFatal(true);
}

/** FLR 处理失败：置位 FAIL 与 DEV_FATAL */
void
BootCtrl::flrFail()
{
    if (flrFailed)
        return;
    flrFailed = true;
    setDevFatal();
    if (stage != 4)
        latchLinkFault();
    if (flrTimer.scheduled())
        em.deschedule(flrTimer);
}

/** ML 的链路状态或错误变化时调用 */
void
BootCtrl::mlChanged()
{
    if (!ml)
        return;
    if (ml->state() == MlLinkState::Failed) {
        setDevFatal();
        if (stage != 4)
            latchLinkFault();
        if (flrProcessing())
            flrFail();
    }
    updateFlrTimer();
    trySend();
}

/** 按 ML 是否为 UP 启动或停止处理超时的计时 */
void
BootCtrl::updateFlrTimer()
{
    bool run = flrProcessing() && !flrFailed && ml &&
               ml->state() == MlLinkState::Up;
    if (run && !flrTimer.scheduled()) {
        flrTimerStart = curTick();
        em.schedule(flrTimer, curTick() + flrRemaining);
    } else if (!run && flrTimer.scheduled()) {
        flrRemaining -= curTick() - flrTimerStart;
        em.deschedule(flrTimer);
    }
    if (!flrProcessing())
        flrRemaining = flrTimeoutTicks;
}

/** 处理超时到达 */
void
BootCtrl::flrTimeout()
{
    DPRINTF(Gw100Boot, "FLR 处理超时\n");
    flrFail();
}

} // namespace gw100
} // namespace gem5
