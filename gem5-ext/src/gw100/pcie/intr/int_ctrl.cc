/**
 * @file  int_ctrl.cc
 * @brief IntCtrl 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/intr/int_ctrl.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** INT_STATUS 中 CTX[0] 的位号 */
constexpr unsigned CtxBit0 = 16;
/** INT_STATUS.ERROR 的位号 */
constexpr unsigned ErrorBit = 1;

} // anonymous namespace

/**
 * 构造：status 提供 INT_STATUS 与中断事件，host 是发送通路，
 * vectors 为表项个数，contexts 为上下文个数
 */
IntCtrl::IntCtrl(StatusUnit &status, HdmaHostIf &host, unsigned vectors,
                 unsigned contexts)
    : status(status), host(host), nvec(vectors), nctx(contexts)
{
    status.onIntEvent = [this](uint32_t ev, uint32_t cl) {
        intEvent(ev, cl);
    };
    reset();
}

/** INT_STATUS 的位 b 对应的向量（事件时的映射） */
unsigned
IntCtrl::vectorOf(unsigned b) const
{
    if (b >= CtxBit0 && b < CtxBit0 + nctx)
        return ctxVec[b - CtxBit0] % nvec;
    return b == ErrorBit ? 1 : 0;   // ADMIN、FW_EVENT 用 0，ERROR 用 1
}

/** 读表项 v 的第 w 个 32 位字（0 地址低、1 地址高、2 数据、3 控制） */
uint32_t
IntCtrl::tableRead(unsigned v, unsigned w) const
{
    const Entry &e = table[v];
    switch (w) {
      case 0: return e.addrLo;
      case 1: return e.addrHi;
      case 2: return e.data;
      default: return e.ctrl;
    }
}

/** 写表项 v 的第 w 个 32 位字 */
void
IntCtrl::tableWrite(unsigned v, unsigned w, uint32_t val)
{
    Entry &e = table[v];
    switch (w) {
      case 0: e.addrLo = val & ~3u; break;   // 消息地址 4 B 对齐
      case 1: e.addrHi = val; break;
      case 2: e.data = val; break;
      default: e.ctrl = val & 1; break;
    }
    trySend();
}

/** MSI-X Enable 与 Function Mask 的当前值（配置写之后调用） */
void
IntCtrl::setMsix(bool enable, bool func_mask)
{
    if (enable != msixEn)
        clearPending();   // 模式切换：W、S、Pending 全部清零（§8.4）
    msixEn = enable;
    funcMask = func_mask;
    updateIntx();
    trySend();
}

/** Bus Master Enable 的当前值 */
void
IntCtrl::setBusMaster(bool en)
{
    bme = en;
    trySend();
}

/** Command 的 Interrupt Disable 的当前值 */
void
IntCtrl::setIntxDisable(bool dis)
{
    intxDis = dis;
    updateIntx();
}

/** FLR 门控（spec-pcie §13.2）：期间不发消息、不置 INTA */
void
IntCtrl::setFlrGate(bool gate)
{
    flrGate = gate;
    updateIntx();
    trySend();
}

/** 固件写 INT_REQ；FLR 门控期间被忽略 */
void
IntCtrl::request(uint32_t bits)
{
    if (!flrGate)
        status.request(bits);
}

/** 固件写 INT_LEVEL；FLR 门控期间被忽略 */
void
IntCtrl::setLevel(uint32_t v)
{
    if (!flrGate)
        status.setLevel(v);
}

/** 固件写 CTX_VECTOR[n]；FLR 门控期间被忽略 */
void
IntCtrl::setCtxVector(unsigned n, uint32_t v)
{
    if (!flrGate && n < nctx)
        ctxVec[n] = v & 0x1F;
}

/** 撤销全部待发、来源与 Pending */
void
IntCtrl::clearPending()
{
    want = 0;
    pend = 0;
    src.fill(0);
}

/**
 * 复位（FLR、设备完全复位）：表项恢复复位值（Mask = 1），待发、
 * 来源、Pending 清零，CTX_VECTOR 恢复复位值；MSI-X 的配置由所属
 * 设备随后经 setMsix() 告知
 */
void
IntCtrl::reset()
{
    table.fill(Entry{});
    clearPending();
    rr = 0;
    for (unsigned n = 0; n < MaxContexts; n++)
        ctxVec[n] = 2 + n;
}

/** StatusUnit 报告的中断事件与清零 */
void
IntCtrl::intEvent(uint32_t events, uint32_t cleared)
{
    if (msixEn) {
        for (unsigned b = 0; b < 32; b++) {
            if (!(events >> b & 1))
                continue;
            unsigned v = vectorOf(b);
            want |= 1u << v;
            src[v] |= 1u << b;
        }
    }
    // 实际变为 0 的位从所有来源集合中删去；集合变空则撤销待发
    if (cleared) {
        for (unsigned v = 0; v < nvec; v++) {
            if (!(src[v] & cleared))
                continue;
            src[v] &= ~cleared;
            if (src[v] == 0) {
                want &= ~(1u << v);
                pend &= ~(1u << v);
            }
        }
    }
    updateIntx();
    trySend();
}

/** 向量 v 是否被 Function Mask、表项 Mask 或 BME 阻止 */
bool
IntCtrl::blocked(unsigned v) const
{
    return funcMask || (table[v].ctrl & 1) || !bme;
}

/** 发出可以发出的消息（发送通路空闲、条件变化时调用） */
void
IntCtrl::trySend()
{
    if (!msixEn || flrGate || want == 0)
        return;
    bool tx_full = false;
    for (unsigned k = 0; k < nvec && want; k++) {
        unsigned v = (rr + k) % nvec;
        if (!(want >> v & 1))
            continue;
        if (blocked(v)) {
            pend |= 1u << v;   // 粘滞到消息发出或事件被处理
            continue;
        }
        if (tx_full || !host.canWrite()) {
            tx_full = true;    // 发送通路满：等它空出来再调用
            continue;
        }
        const Entry &e = table[v];
        Addr a = (uint64_t(e.addrHi) << 32) | e.addrLo;
        uint8_t data[4];
        for (unsigned i = 0; i < 4; i++)
            data[i] = uint8_t(e.data >> (8 * i));
        host.write(a, 4, data);
        want &= ~(1u << v);
        src[v] = 0;
        pend &= ~(1u << v);
        rr = (v + 1) % nvec;
    }
}

/** 按当前状态更新 INTA */
void
IntCtrl::updateIntx()
{
    bool lv = !msixEn && !intxDis && !flrGate &&
              (status.intStatus() & ~status.intMask()) != 0;
    if (lv == inta)
        return;
    inta = lv;
    if (onIntx)
        onIntx(lv);
}

} // namespace gw100
} // namespace gem5

namespace gem5::gw100
{

/** 保存MSI-X 表、来源、Pending 和功能门控。 */
void
IntCtrl::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(want);
    SERIALIZE_SCALAR(pend);
    SERIALIZE_SCALAR(rr);
    SERIALIZE_SCALAR(msixEn);
    SERIALIZE_SCALAR(funcMask);
    SERIALIZE_SCALAR(bme);
    SERIALIZE_SCALAR(intxDis);
    SERIALIZE_SCALAR(flrGate);
    SERIALIZE_SCALAR(inta);
    SERIALIZE_CONTAINER(src);
    SERIALIZE_CONTAINER(ctxVec);
    for (unsigned index = 0; index < table.size(); ++index) {
        ScopedCheckpointSection section(cp, "vector" + std::to_string(index));
        paramOut(cp, "addrLo", table[index].addrLo);
        paramOut(cp, "addrHi", table[index].addrHi);
        paramOut(cp, "data", table[index].data);
        paramOut(cp, "ctrl", table[index].ctrl);
    }
}

/** 恢复MSI-X 表、来源、Pending 和功能门控。 */
void
IntCtrl::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(want);
    UNSERIALIZE_SCALAR(pend);
    UNSERIALIZE_SCALAR(rr);
    UNSERIALIZE_SCALAR(msixEn);
    UNSERIALIZE_SCALAR(funcMask);
    UNSERIALIZE_SCALAR(bme);
    UNSERIALIZE_SCALAR(intxDis);
    UNSERIALIZE_SCALAR(flrGate);
    UNSERIALIZE_SCALAR(inta);
    arrayParamIn(cp, "src", src.data(), src.size());
    arrayParamIn(cp, "ctxVec", ctxVec.data(), ctxVec.size());
    for (unsigned index = 0; index < table.size(); ++index) {
        ScopedCheckpointSection section(cp, "vector" + std::to_string(index));
        paramIn(cp, "addrLo", table[index].addrLo);
        paramIn(cp, "addrHi", table[index].addrHi);
        paramIn(cp, "data", table[index].data);
        paramIn(cp, "ctrl", table[index].ctrl);
    }
}

} // namespace gem5::gw100
