/**
 * @file  hdma_engine.cc
 * @brief HdmaEngine 的实现
 *
 * @see spec-host-dma, spec-pcie
 */

#include "src/gw100/pcie/hdma/hdma_engine.hh"

#include <algorithm>
#include <cstring>
#include "base/logging.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 描述符 TYPE：H2D */
constexpr uint8_t TypeH2d = 1;
/** 描述符 TYPE：D2H */
constexpr uint8_t TypeD2h = 2;
/** CH_CTRL.EN */
constexpr uint32_t CtrlEn = 1u << 0;
/** CH_CTRL.PAUSE */
constexpr uint32_t CtrlPause = 1u << 1;
/** CH_CTRL.ABORT（W1S） */
constexpr uint32_t CtrlAbort = 1u << 2;
/** CH_CTRL.RESUME（W1S） */
constexpr uint32_t CtrlResume = 1u << 3;
/** CH_CTRL 的 RW 位：EN、PAUSE、WEIGHT */
constexpr uint32_t CtrlRwMask = CtrlEn | CtrlPause | (0xFu << 8);
/** 事件位：HDMA_NOTIFY（spec-mgmt-tile §6.3） */
constexpr uint32_t EvtNotify = 1u << 5;
/** 事件位：HDMA_ERR */
constexpr uint32_t EvtErr = 1u << 6;
/** 通道寄存器的起始 */
constexpr Addr ChanBase = 0x100;
/** 每个通道寄存器的跨度 */
constexpr Addr ChanStride = 0x40;
/** 停顿计数的下标：tag 或完成缓冲 */
constexpr unsigned StallTag = 0;
/** 停顿计数的下标：D2H 暂存缓冲 */
constexpr unsigned StallD2h = 1;
/** 停顿计数的下标：NoC 反压 */
constexpr unsigned StallNoc = 2;
/** 停顿计数的下标：PCIe 发送 */
constexpr unsigned StallTx = 3;

/** 读小端的 32 位字 */
uint32_t
le32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}

/** 读小端的 64 位字 */
uint64_t
le64(const uint8_t *p)
{
    return le32(p) | (uint64_t(le32(p + 4)) << 32);
}

/** host 读请求的完成状态对应的错误码 */
HdmaErr
hostErr(HostStatus st)
{
    switch (st) {
      case HostStatus::Ur: return HdmaErr::HostUr;
      case HostStatus::Ca: return HdmaErr::HostCa;
      case HostStatus::Poisoned: return HdmaErr::HostPoisoned;
      default: return HdmaErr::Ok;
    }
}

} // anonymous namespace

/** 按参数构造；各接口由所属设备提供 */
HdmaEngine::HdmaEngine(const Config &cfg, HdmaClockIf &clk,
                       HdmaHostIf &host, HdmaDevIf &dev, HdmaMlIf &ml)
    : cfg(cfg), clk(clk), host(host), dev(dev), ml(ml),
      chans(cfg.channels)
{
}

/** 数据通路传送 len 字节所需的周期数 */
Tick
HdmaEngine::cycles(unsigned len) const
{
    return (len + cfg.datapathBytes - 1) / cfg.datapathBytes;
}

/** 请求在不早于 when 的时钟沿 tick */
void
HdmaEngine::wakeAt(Tick when)
{
    if (when >= wakePending)
        return;
    wakePending = when;
    clk.wakeAt(when);
}

/** 有新的进展可能（回调、端口空闲）时调用：在下一个时钟沿 tick */
void
HdmaEngine::wake()
{
    wakeAt(clk.now());
}

/** 通道能否开始新的描述符 */
bool
HdmaEngine::canRun(const Channel &ch) const
{
    return globalEn && !held && (ch.ctrl & CtrlEn) &&
           !(ch.ctrl & CtrlPause) && ch.state != ErrStop && !ch.stopping &&
           !ch.aborting && !ch.wbFailed;
}

/** 在通道的描述符中按编号查找；找不到返回 nullptr */
HdmaEngine::Desc *
HdmaEngine::findDesc(unsigned c, uint64_t serial)
{
    for (auto &d : chans[c].descs) {
        if (d.serial == serial)
            return &d;
    }
    return nullptr;
}

/** 本通道当前发请求的描述符（已开始、未发完）；没有返回 nullptr */
HdmaEngine::Desc *
HdmaEngine::issuingDesc(unsigned c)
{
    for (auto &d : chans[c].descs) {
        if (d.started && !d.issuedAll)
            return &d;
    }
    return nullptr;
}

/** 读取描述符：最多预取 2 个 */
void
HdmaEngine::fetchDescs(unsigned c)
{
    Channel &ch = chans[c];
    while (canRun(ch) && ch.descs.size() < 2 && ch.fetchIdx != ch.tail &&
           (ch.descs.empty() || !ch.descs.back().fetchFailed)) {
        Desc d;
        d.idx = ch.fetchIdx;
        d.serial = nextSerial++;
        uint64_t serial = d.serial;
        uint64_t g = gen;
        ch.descs.push_back(d);
        bool ok = ml.descRead(slotAddr(ch, d.idx),
            [this, c, serial, g](MlStatus s, const MlData &raw) {
                if (g != gen)
                    return;
                Desc *dp = findDesc(c, serial);
                if (!dp)
                    return;
                if (s != MlStatus::Ok) {
                    // 读取失败的描述符挡住它之后的描述符；它之前的照常
                    // 处理，都结束后进入错误停止（§8.2）
                    dp->fetchFailed = true;
                } else {
                    parseDesc(c, *dp, raw);
                    dp->fetched = true;
                }
                wake();
            });
        if (!ok) {
            ch.descs.pop_back();  // 链路尚未建立，稍后重试
            return;
        }
        ch.fetchIdx = (ch.fetchIdx + 1) & (entries(ch) - 1);
        ch.state = Running;
        progress = true;
    }
}

/** 解析 DESC_RD 返回的 32 B */
void
HdmaEngine::parseDesc(unsigned c, Desc &d, const MlData &raw)
{
    uint32_t w[8];
    for (unsigned i = 0; i < 8; i++)
        w[i] = le32(raw.data() + 4 * i);
    d.type = w[0] & 0xFF;
    d.sgl = (w[0] >> 8) & 1;
    bool no_snoop = (w[0] >> 9) & 1;
    bool relaxed = (w[0] >> 10) & 1;
    d.attributes = no_snoop | (relaxed << 1);
    d.notify = (w[0] >> 11) & 1;
    d.fence = (w[0] >> 12) & 1;
    d.len = w[1];
    d.host = w[2] | (uint64_t(w[3]) << 32);
    d.dev = w[4] | (uint64_t(w[5]) << 32);
    d.segBytes = w[6] & 0xFFFF;

    // DESC_INVALID：TYPE、保留位、LENGTH、对齐、通道 0 的属性（§3.2、
    // §3.3、§5.4）
    bool bad = (d.type != TypeH2d && d.type != TypeD2h) ||
               ((w[0] >> 13) & 7) != 0 || d.len == 0 ||
               d.len > cfg.maxDescBytes || d.len % 4 != 0 ||
               d.dev % 4 != 0 ||
               (cfg.devicePaBits < 64 && (d.dev >> cfg.devicePaBits)) ||
               (!d.sgl && (d.host % 4 != 0 || d.segBytes != 0)) ||
               (c == 0 && (no_snoop || relaxed));
    if (bad) {
        d.err = HdmaErr::DescInvalid;
        return;
    }
    // SGL 的第一段（§4.2 S1）
    if (d.sgl && (d.host % 16 != 0 || d.segBytes == 0 ||
                  d.segBytes % 16 != 0 || d.segBytes > cfg.sglSegMax)) {
        d.err = HdmaErr::SglInvalid;
        d.errAddr = d.host;
        return;
    }
    // 设备一侧的地址范围（v0.1 只有设备内存）
    if (d.dev < cfg.devBase || d.dev + d.len > cfg.devBase + cfg.devSize) {
        d.err = HdmaErr::DevAddr;
        d.errAddr = d.dev;
    }
}

/** 描述符 d 出错：停止发出新请求，记录第一个错误 */
void
HdmaEngine::failDesc(unsigned c, Desc &d, HdmaErr err, Addr addr)
{
    if (d.err == HdmaErr::Ok) {
        d.err = err;
        d.errAddr = addr;
    }
    d.issuedAll = true;
    chans[c].stopping = true;
    progress = true;
}

/** 开始可以开始的描述符（校验字段，准备 SGL） */
void
HdmaEngine::startDescs(unsigned c)
{
    Channel &ch = chans[c];
    for (size_t i = 0; i < ch.descs.size(); i++) {
        Desc &d = ch.descs[i];
        if (d.started)
            continue;
        if (!canRun(ch) || !d.fetched || d.fetchFailed)
            return;
        // FENCE 等前面的全部完成（已回写）；否则等前一个发完请求
        if (i > 0 && (d.fence || !ch.descs[i - 1].issuedAll))
            return;
        d.started = true;
        progress = true;
        if (d.err != HdmaErr::Ok) {
            failDesc(c, d, d.err, d.errAddr);
            continue;
        }
        if (!busMaster) {
            failDesc(c, d, HdmaErr::BusMasterOff, 0);
            continue;
        }
        if (d.sgl) {
            Sgl &s = ch.sgl;
            s = Sgl{};
            s.owner = d.serial;
            s.segAddr = d.host;
            s.segBytes = d.segBytes;
            s.seg.assign(d.segBytes, 0);
            s.segCount = 1;
        }
    }
}

/** 把 SGL 段的已到达部分解析成条目：失败返回错误码 */
HdmaErr
HdmaEngine::nextSglEntry(unsigned c, Desc &d)
{
    Sgl &s = chans[c].sgl;
    unsigned n = s.segBytes / 16;
    if (s.entry >= n)
        return HdmaErr::SglInvalid;  // 条目用完而数据未满（S5）
    const uint8_t *e = s.seg.data() + 16 * s.entry;
    Addr a = le64(e);
    uint32_t l = le32(e + 8);
    if (e[13] || e[14] || e[15])
        return HdmaErr::SglInvalid;
    if (e[12] == 0) {
        // DATA（S3、S5）
        if (a % 4 != 0 || l == 0 || l % 4 != 0 || l > d.len - d.issued)
            return HdmaErr::SglInvalid;
        s.dataAddr = a;
        s.dataLeft = l;
        s.dataInSeg = true;
        s.entry++;
        return HdmaErr::Ok;
    }
    if (e[12] == 1) {
        // SEGMENT（S1、S2、S8、S9）
        if (s.entry != n - 1 || !s.dataInSeg || a % 16 != 0 || l == 0 ||
            l % 16 != 0 || l > cfg.sglSegMax ||
            s.segCount + 1 > cfg.sglMaxSegments)
            return HdmaErr::SglInvalid;
        s.segAddr = a;
        s.segBytes = l;
        s.seg.assign(l, 0);
        s.requested = 0;
        s.arrived = 0;
        s.entry = 0;
        s.segCount++;
        s.dataInSeg = false;
        return HdmaErr::Ok;
    }
    return HdmaErr::SglInvalid;
}

/**
 * 当前 host 地址段：成功时给出地址和这一段剩余的字节数；SGL 段尚未
 * 到达返回 false；SGL 非法时让描述符失败并返回 false
 */
bool
HdmaEngine::hostSpan(unsigned c, Desc &d, Addr &addr, uint64_t &left)
{
    if (!d.sgl) {
        addr = d.host + d.issued;
        left = d.len - d.issued;
        return true;
    }
    Sgl &s = chans[c].sgl;
    while (s.dataLeft == 0) {
        if (s.arrived < s.segBytes)
            return false;
        Addr at = s.segAddr + 16 * s.entry;
        HdmaErr e = nextSglEntry(c, d);
        if (e != HdmaErr::Ok) {
            failDesc(c, d, e, at);
            return false;
        }
    }
    addr = s.dataAddr;
    left = s.dataLeft;
    return true;
}

/** host 地址前进 n 字节（只对 SGL 有意义） */
void
HdmaEngine::advanceHost(unsigned c, uint64_t n)
{
    Sgl &s = chans[c].sgl;
    if (s.dataLeft >= n) {
        s.dataLeft -= n;
        s.dataAddr += n;
    }
}

/** 本通道的读方向是否有可以发出的请求；有则给出地址和长度 */
bool
HdmaEngine::readCandidate(unsigned c, ReadKind &kind, Addr &addr,
                          unsigned &len)
{
    Desc *d = issuingDesc(c);
    if (!d || !busMaster || chans[c].aborting)
        return false;
    unsigned max_read = std::min(pcie.mrrs, cfg.maxReadReq);
    Sgl &s = chans[c].sgl;
    if (d->sgl && s.dataLeft == 0 && s.requested < s.segBytes) {
        Addr a = s.segAddr + s.requested;
        uint64_t n = std::min<uint64_t>(s.segBytes - s.requested,
                                        max_read);
        n = std::min<uint64_t>(n, 4096 - (a & 4095));
        kind = ReadKind::Sgl;
        addr = a;
        len = n;
        return true;
    }
    if (d->type != TypeH2d)
        return false;
    Addr a;
    uint64_t left;
    if (!hostSpan(c, *d, a, left))
        return false;
    uint64_t n = std::min({left, d->len - d->issued, uint64_t(max_read),
                           uint64_t(4096 - (a & 4095))});
    kind = ReadKind::Data;
    addr = a;
    len = n;
    return true;
}

/**
 * 在 cand(c) 为真的通道中选一个，不改变轮询状态：通道 0 严格优先，
 * 其余加权轮询；last 是该方向的轮询位置，dir 是方向（0 读、1 写、
 * 2 设备内存读）。没有返回 -1
 */
int
HdmaEngine::arbitrate(const std::function<bool(unsigned)> &cand,
                      unsigned last, unsigned dir)
{
    unsigned n = chans.size();
    if (n == 0)
        return -1;
    if (cand(0))
        return 0;
    if (n == 1)
        return -1;
    if (last == 0 || last >= n)
        last = n - 1;
    if (chans[last].credit[dir] > 0 && cand(last))
        return last;
    for (unsigned k = 1; k < n; k++) {
        unsigned c = 1 + (last - 1 + k) % (n - 1);
        if (cand(c))
            return c;
    }
    return -1;
}

/** 通道 c 在方向 dir 上发出了一个请求：更新轮询位置与配额 */
void
HdmaEngine::commitArb(unsigned c, unsigned &last, unsigned dir)
{
    if (c == 0)
        return;
    if (c == last && chans[c].credit[dir] > 0) {
        chans[c].credit[dir]--;
        return;
    }
    unsigned w = (chans[c].ctrl >> 8) & 0xF;
    chans[c].credit[dir] = (w == 0 ? 1 : w) - 1;
    last = c;
}

/** 在途读请求数是否已达上限 */
bool
HdmaEngine::tagsFull() const
{
    unsigned avail = pcie.extTag ? 256 : 32;
    unsigned limit = avail > tagsQuarantined ? avail - tagsQuarantined : 0;
    limit = std::min(limit, cfg.maxOutstandingReads);
    return tagsInUse >= limit;
}

/** 分配一个 tag；没有可用的返回 -1 */
int
HdmaEngine::allocTag()
{
    unsigned avail = pcie.extTag ? 256 : 32;
    int best = -1;
    for (unsigned t = 0; t < avail; t++) {
        const Tag &tg = tags[t];
        if (tg.inUse || tg.quarantined)
            continue;
        if (best < 0 || tg.lastUsed < tags[best].lastUsed)
            best = t;  // 最久未用的优先（spec-pcie §10.3）
    }
    return best;
}

/** 读方向：发出一个 MRd（SGL 段或 H2D 数据） */
bool
HdmaEngine::issueRead()
{
    Tick now = clk.now();
    ReadKind kind;
    Addr addr;
    unsigned len;
    int c = arbitrate([&](unsigned ch) {
        return readCandidate(ch, kind, addr, len);
    }, rrRead, 0);
    if (c < 0)
        return false;
    if (rdFreeAt > now) {
        soonest = std::min(soonest, rdFreeAt);
        return false;
    }
    readCandidate(c, kind, addr, len);
    if (!host.canRead(addr, len)) {
        stallNow[StallTx] = true;
        return false;
    }
    if (tagsFull() || cplUsed + len > cfg.cplBuffer) {
        stallNow[StallTag] = true;
        return false;
    }
    int t = allocTag();
    if (t < 0) {
        stallNow[StallTag] = true;
        return false;
    }
    Channel &ch = chans[c];
    Desc *d = issuingDesc(c);
    Tag &tg = tags[t];
    tg.inUse = true;
    tg.submitted = false;
    tg.use++;
    tg.lastUsed = now;
    tg.deadline = MaxTick;
    tg.kind = kind;
    tg.chan = c;
    tg.serial = d->serial;
    tg.addr = addr;
    tg.len = len;
    tagsInUse++;
    cplUsed += len;
    d->outstanding++;
    ch.inflight++;
    uint64_t use = tg.use;
    bool accepted = host.readTagged(
        addr, len, t,
        [this, t, use] {
            Tag &tag = tags[t];
            if (!tag.inUse || tag.use != use) {
                return;
            }
            tag.submitted = true;
            tag.deadline = clk.now() + (pcie.cplTimeout ? pcie.cplTimeout
                                                        : cfg.cplTimeout);
            nextTagEvent = std::min(nextTagEvent, tag.deadline);
            wakeAt(tag.deadline);
        },
        [this, t, use](HostStatus st, const uint8_t *p) {
            readDone(t, use, st, p);
        },
        kind == ReadKind::Data
            ? d->attributes & (unsigned(pcie.noSnoop) | (pcie.relaxed << 1))
            : 0);
    if (!accepted) {
        tg.inUse = false;
        --tagsInUse;
        cplUsed -= len;
        --d->outstanding;
        --ch.inflight;
        stallNow[StallTx] = true;
        return false;
    }
    if (kind == ReadKind::Data) {
        tg.dest = d->dev + d->issued;
        d->issued += len;
        advanceHost(c, len);
        if (d->issued == d->len)
            d->issuedAll = true;
    } else {
        tg.dest = ch.sgl.requested;
        ch.sgl.requested += len;
    }
    commitArb(c, rrRead, 0);
    rdFreeAt = now + clk.period();
    progress = true;
    return true;
}

/** host 读请求完成 */
void
HdmaEngine::readDone(unsigned t, uint64_t use, HostStatus st,
                     const uint8_t *data)
{
    Tag &tg = tags[t];
    if (tg.use != use)
        return;
    if (tg.quarantined) {
        // 超时或复位之后到达：丢弃数据，释放 tag（spec-pcie §10.3）
        tg.quarantined = false;
        tagsQuarantined--;
        wake();
        return;
    }
    if (!tg.inUse)
        return;
    tg.inUse = false;
    tagsInUse--;
    Channel &ch = chans[tg.chan];
    ch.inflight--;
    Desc *d = findDesc(tg.chan, tg.serial);
    if (!d) {
        cplUsed -= tg.len;
        wake();
        return;
    }
    d->outstanding--;
    if (st != HostStatus::Ok) {
        cplUsed -= tg.len;
        failDesc(tg.chan, *d, hostErr(st), tg.addr);
        hostError();
    } else if (d->err != HdmaErr::Ok || ch.aborting || ch.wbFailed) {
        cplUsed -= tg.len;  // 出错、中止之后到达的数据丢弃
    } else if (tg.kind == ReadKind::Sgl) {
        Sgl &s = ch.sgl;
        if (s.owner == d->serial) {
            std::memcpy(s.seg.data() + tg.dest, data, tg.len);
            s.arrived += tg.len;
        }
        cplUsed -= tg.len;
    } else {
        DevWrite w;
        w.chan = tg.chan;
        w.serial = tg.serial;
        w.dpa = tg.dest;
        w.data.assign(data, data + tg.len);
        devWrites.push_back(std::move(w));
        d->outstanding++;  // 这批数据写入设备内存之前，描述符没有结束
    }
    wake();
}

/** H2D：把已到达的数据写入设备内存 */
bool
HdmaEngine::issueDevWrite()
{
    if (devWrites.empty())
        return false;
    Tick now = clk.now();
    DevWrite &w = devWrites.front();
    Channel &ch = chans[w.chan];
    Desc *d = findDesc(w.chan, w.serial);
    if (!d || d->err != HdmaErr::Ok || ch.aborting || ch.wbFailed) {
        cplUsed -= w.data.size() - w.done;
        if (d)
            d->outstanding--;
        devWrites.pop_front();
        progress = true;
        return true;
    }
    if (devWrFreeAt > now) {
        soonest = std::min(soonest, devWrFreeAt);
        return false;
    }
    if (!dev.canSend()) {
        stallNow[StallNoc] = true;
        return false;
    }
    Addr dpa = w.dpa + w.done;
    unsigned piece = std::min<unsigned>(w.data.size() - w.done,
                                        cfg.devChunk - dpa % cfg.devChunk);
    unsigned c = w.chan;
    uint64_t serial = w.serial;
    uint64_t g = gen;
    d->outstanding++;
    ch.inflight++;
    dev.write(dpa, piece, w.data.data() + w.done,
        [this, c, serial, piece, dpa, g](bool ok) {
            if (g != gen)
                return;
            Channel &ch = chans[c];
            ch.inflight--;
            cplUsed -= piece;
            Desc *d = findDesc(c, serial);
            if (d) {
                d->outstanding--;
                if (!ok) {
                    failDesc(c, *d, HdmaErr::DevAddr, dpa);
                } else if (d->err == HdmaErr::Ok) {
                    ch.bytes += piece;
                    bytesH2d += piece;
                }
            }
            wake();
        });
    w.done += piece;
    if (w.done == w.data.size()) {
        d->outstanding--;
        devWrites.pop_front();
    }
    devWrFreeAt = now + cycles(piece) * clk.period();
    progress = true;
    return true;
}

/** D2H：发出一个设备内存读 */
bool
HdmaEngine::issueDevRead()
{
    Tick now = clk.now();
    int c = arbitrate([&](unsigned ch) {
        Desc *d = issuingDesc(ch);
        return d && d->type == TypeD2h && d->devIssued < d->len &&
               !chans[ch].aborting;
    }, rrDevRead, 2);
    if (c < 0)
        return false;
    if (devRdFreeAt > now) {
        soonest = std::min(soonest, devRdFreeAt);
        return false;
    }
    Channel &ch = chans[c];
    Desc *d = issuingDesc(c);
    Addr dpa = d->dev + d->devIssued;
    unsigned n = std::min<uint64_t>(d->len - d->devIssued,
                                    cfg.devChunk - dpa % cfg.devChunk);
    if (d2hUsed + n > cfg.d2hBuffer) {
        stallNow[StallD2h] = true;
        return false;
    }
    if (!dev.canSend()) {
        stallNow[StallNoc] = true;
        return false;
    }
    uint64_t off = d->devIssued;
    Chunk ck;
    ck.off = off;
    ck.len = n;
    d->staging.push_back(std::move(ck));
    d->devIssued += n;
    d->outstanding++;
    ch.inflight++;
    d2hUsed += n;
    uint64_t serial = d->serial;
    uint64_t g = gen;
    dev.read(dpa, n, [this, c, serial, off, n, dpa, g](bool ok,
                                                     const uint8_t *p) {
        if (g != gen)
            return;
        chans[c].inflight--;
        Desc *d = findDesc(c, serial);
        if (d) {
            d->outstanding--;
            if (!ok) {
                failDesc(c, *d, HdmaErr::DevAddr, dpa);
            } else {
                for (auto &ck : d->staging) {
                    if (ck.off == off) {
                        ck.arrived = true;
                        ck.data.assign(p, p + n);
                        break;
                    }
                }
            }
        }
        wake();
    });
    commitArb(c, rrDevRead, 2);
    devRdFreeAt = now + clk.period();
    progress = true;
    return true;
}

/** D2H 暂存缓冲中描述符 d 尚未写出的字节数 */
unsigned
HdmaEngine::staged(const Desc &d)
{
    unsigned n = 0;
    for (const auto &ck : d.staging)
        n += ck.len;
    return n - d.frontUsed;
}

/** 从 D2H 暂存缓冲中取出描述符 d 的前 len 字节 */
void
HdmaEngine::takeStaged(Desc &d, unsigned len, uint8_t *out)
{
    while (len > 0) {
        Chunk &ck = d.staging.front();
        unsigned n = std::min(len, ck.len - d.frontUsed);
        std::memcpy(out, ck.data.data() + d.frontUsed, n);
        out += n;
        len -= n;
        d.frontUsed += n;
        if (d.frontUsed == ck.len) {
            d.staging.pop_front();
            d.frontUsed = 0;
        }
    }
}

/** 本通道的写方向是否有可以发出的 MWr；有则给出地址和长度 */
bool
HdmaEngine::writeCandidate(unsigned c, Addr &addr, unsigned &len)
{
    Desc *d = issuingDesc(c);
    if (!d || d->type != TypeD2h || !busMaster || chans[c].aborting)
        return false;
    // 已到达、可以写出的连续字节数
    uint64_t ready = 0;
    unsigned used = d->frontUsed;
    for (const auto &ck : d->staging) {
        if (!ck.arrived)
            break;
        ready += ck.len - used;
        used = 0;
    }
    if (ready == 0)
        return false;
    Addr a;
    uint64_t left;
    if (!hostSpan(c, *d, a, left))
        return false;
    uint64_t n = std::min({left, ready, d->len - d->issued,
                           uint64_t(pcie.mps),
                           uint64_t(4096 - (a & 4095))});
    addr = a;
    len = n;
    return true;
}

/** 写方向：发出一个 MWr（D2H 数据） */
bool
HdmaEngine::issueWrite()
{
    Tick now = clk.now();
    Addr addr;
    unsigned len;
    int c = arbitrate([&](unsigned ch) {
        return writeCandidate(ch, addr, len);
    }, rrWrite, 1);
    if (c < 0)
        return false;
    if (wrFreeAt > now) {
        soonest = std::min(soonest, wrFreeAt);
        return false;
    }
    if (!host.canWrite()) {
        stallNow[StallTx] = true;
        return false;
    }
    writeCandidate(c, addr, len);
    Channel &ch = chans[c];
    Desc *d = issuingDesc(c);
    std::vector<uint8_t> buf(len);
    takeStaged(*d, len, buf.data());
    d2hUsed -= len;
    host.write(addr, len, buf.data(),
               d->attributes & (unsigned(pcie.noSnoop) | (pcie.relaxed << 1)));
    d->issued += len;
    advanceHost(c, len);
    if (d->issued == d->len)
        d->issuedAll = true;
    ch.bytes += len;
    bytesD2h += len;
    commitArb(c, rrWrite, 1);
    wrFreeAt = now + (1 + cycles(len)) * clk.period();
    progress = true;
    return true;
}

/** 回写已结束的描述符，按环的顺序 */
void
HdmaEngine::writeBack(unsigned c)
{
    Channel &ch = chans[c];
    if (ch.aborting || ch.wbFailed || ch.descs.empty())
        return;
    Desc &d = ch.descs.front();
    if (d.wbPending || !descDone(d))
        return;
    uint32_t dw7 = 1u | (uint32_t(d.err) << 8);
    uint64_t serial = d.serial;
    uint64_t g = gen;
    d.wbPending = true;
    bool ok = ml.descWrite(slotAddr(ch, d.idx), dw7,
        [this, c, serial, g](MlStatus s) {
            if (g != gen)
                return;
            Channel &ch = chans[c];
            if (ch.descs.empty() || ch.descs.front().serial != serial)
                return;
            Desc &d = ch.descs.front();
            if (s != MlStatus::Ok) {
                writeBackFailed(c, d.idx);
                wake();
                return;
            }
            if (d.err != HdmaErr::Ok && !ch.pending.valid)
                ch.pending = PendingErr{true, d.err, PhaseData, d.idx,
                                        d.errAddr};
            bool notify = d.notify;
            d2hUsed -= staged(d);
            if (ch.sgl.owner == d.serial)
                ch.sgl.owner = 0;
            ch.descs.pop_front();
            ch.head = (ch.head + 1) & (entries(ch) - 1);
            ch.descDone++;
            if (notify)
                pendingEvents |= EvtNotify;
            wake();
        });
    if (!ok)
        d.wbPending = false;  // 链路尚未建立，稍后重试
    else
        progress = true;
}

/** 回写失败：通道立即进入错误停止（§8.2） */
void
HdmaEngine::writeBackFailed(unsigned c, unsigned idx)
{
    Channel &ch = chans[c];
    ch.wbFailed = true;
    ch.state = ErrStop;
    ch.stopping = false;
    for (auto &d : ch.descs) {
        d.issuedAll = true;
        d.wbPending = false;
    }
    ch.errReg = uint32_t(HdmaErr::DevAddr) | (uint32_t(PhaseDescWrite) << 8) |
                (uint32_t(idx) << 16);
    ch.errAddr = slotAddr(ch, idx);
    ch.pending = PendingErr{};
    pendingEvents |= EvtErr;
    progress = true;
}

/** 已开始的描述符都结束后，进入错误停止（§8.2） */
void
HdmaEngine::maybeErrStop(unsigned c)
{
    Channel &ch = chans[c];
    if (ch.state == ErrStop) {
        // 回写失败：在途操作排空后丢弃这些描述符
        if (ch.wbFailed && !ch.aborting) {
            while (!ch.descs.empty() && ch.descs.front().outstanding == 0) {
                d2hUsed -= staged(ch.descs.front());
                ch.descs.pop_front();
            }
        }
        return;
    }
    bool front_failed = !ch.descs.empty() && ch.descs.front().fetchFailed;
    if ((!ch.stopping && !front_failed) || ch.aborting)
        return;
    for (const auto &d : ch.descs) {
        if (d.started)
            return;  // 等已开始的结束并回写
    }
    if (!ch.descs.empty() && ch.descs.front().fetchFailed &&
        !ch.pending.valid) {
        const Desc &f = ch.descs.front();
        ch.pending = PendingErr{true, HdmaErr::DevAddr, PhaseDescRead, f.idx,
                                slotAddr(ch, f.idx)};
    }
    enterErrStop(c);
}

/** 进入错误停止：写 CH_ERR，丢弃未开始的描述符，发出 HDMA_ERR */
void
HdmaEngine::enterErrStop(unsigned c)
{
    Channel &ch = chans[c];
    ch.state = ErrStop;
    ch.stopping = false;
    for (const auto &d : ch.descs)
        d2hUsed -= staged(d);
    ch.descs.clear();
    ch.fetchIdx = ch.head;
    ch.sgl.owner = 0;
    if (ch.pending.valid) {
        ch.errReg = uint32_t(ch.pending.code) |
                    (uint32_t(ch.pending.phase) << 8) |
                    (uint32_t(ch.pending.idx) << 16);
        ch.errAddr = ch.pending.addr;
        ch.pending = PendingErr{};
    }
    pendingEvents |= EvtErr;
    progress = true;
}

/** 丢弃通道 c 尚未写入设备内存的 H2D 数据 */
void
HdmaEngine::dropDevWrites(unsigned c)
{
    for (auto it = devWrites.begin(); it != devWrites.end();) {
        if (it->chan != c) {
            ++it;
            continue;
        }
        cplUsed -= it->data.size() - it->done;
        if (Desc *d = findDesc(c, it->serial))
            d->outstanding--;
        it = devWrites.erase(it);
    }
}

/** 开始中止通道 c（CH_CTRL.ABORT、HDMA_CTRL.ABORT_ALL） */
void
HdmaEngine::startAbort(unsigned c)
{
    Channel &ch = chans[c];
    if (ch.state != ErrStop && ch.head == ch.tail && ch.inflight == 0 &&
        ch.descs.empty())
        return;  // 已经空闲：没有效果
    ch.aborting = true;
    ch.state = Running;
    ch.stopping = false;
    ch.pending = PendingErr{};
    for (auto &d : ch.descs)
        d.issuedAll = true;
    dropDevWrites(c);  // 尚未发出的请求丢弃
    wake();
}

/** 推进中止：等在途操作排空，再逐个回写 ABORTED（§8.3） */
void
HdmaEngine::advanceAbort(unsigned c)
{
    Channel &ch = chans[c];
    if (!ch.aborting || ch.inflight > 0 || ch.abortWbPending)
        return;
    for (const auto &d : ch.descs) {
        if (d.wbPending || d.outstanding > 0)
            return;
    }
    for (const auto &d : ch.descs)
        d2hUsed -= staged(d);
    ch.descs.clear();
    ch.sgl.owner = 0;
    if (ch.wbFailed)
        ch.tail = ch.head;  // 不回写：TAIL 被置为 HEAD（§8.2）
    if (ch.head == ch.tail) {
        ch.aborting = false;
        ch.wbFailed = false;
        ch.state = Idle;
        ch.fetchIdx = ch.head;
        progress = true;
        return;
    }
    uint64_t g = gen;
    uint32_t dw7 = 1u | (uint32_t(HdmaErr::Aborted) << 8);
    ch.abortWbPending = true;
    bool ok = ml.descWrite(slotAddr(ch, ch.head), dw7,
        [this, c, g](MlStatus s) {
            if (g != gen)
                return;
            Channel &ch = chans[c];
            ch.abortWbPending = false;
            if (s != MlStatus::Ok) {
                ch.aborting = false;
                writeBackFailed(c, ch.head);
            } else {
                ch.head = (ch.head + 1) & (entries(ch) - 1);
                ch.descDone++;
            }
            wake();
        });
    if (!ok)
        ch.abortWbPending = false;
    else
        progress = true;
}

/** 按 PAUSE、空闲更新通道状态 */
void
HdmaEngine::updateState(unsigned c)
{
    Channel &ch = chans[c];
    if (ch.state == ErrStop || ch.aborting)
        return;
    bool started = false;
    for (const auto &d : ch.descs)
        started = started || d.started || !d.fetched;
    if (started || ch.inflight > 0 || ch.stopping)
        ch.state = Running;
    else if (ch.ctrl & CtrlPause)
        ch.state = Paused;
    else if (ch.head != ch.tail && canRun(ch))
        ch.state = Running;
    else
        ch.state = Idle;
}

/** 处理完成超时与隔离期满 */
void
HdmaEngine::checkTags()
{
    Tick now = clk.now();
    if (now < nextTagEvent)
        return;
    nextTagEvent = MaxTick;
    for (unsigned t = 0; t < tags.size(); t++) {
        Tag &tg = tags[t];
        if (tg.inUse && now >= tg.deadline) {
            // 完成超时：tag 进入隔离，描述符失败（spec-pcie §10.3）
            tg.inUse = false;
            tagsInUse--;
            tg.quarantined = true;
            tg.releaseAt = now + cfg.quarantine;
            host.isolateRead(t);
            tagsQuarantined++;
            chans[tg.chan].inflight--;
            cplUsed -= tg.len;
            if (Desc *d = findDesc(tg.chan, tg.serial)) {
                d->outstanding--;
                failDesc(tg.chan, *d, HdmaErr::HostTimeout, tg.addr);
            }
            hostError();
            progress = true;
        } else if (tg.quarantined && now >= tg.releaseAt) {
            tg.quarantined = false;
            tagsQuarantined--;
            progress = true;
        }
        if (tg.inUse)
            nextTagEvent = std::min(nextTagEvent, tg.deadline);
        if (tg.quarantined)
            nextTagEvent = std::min(nextTagEvent, tg.releaseAt);
    }
}

/** 记录 host 读请求的错误：置 ERR_STATUS.DMA_HOST_ERR */
void
HdmaEngine::hostError()
{
    if (onHostError)
        onHostError();
}

/** 按上一次 tick 以来的停顿状态累计停顿周期 */
void
HdmaEngine::accountStalls(Tick now)
{
    Tick per = clk.period();
    uint64_t n = per ? (now - stallAt) / per : 0;
    for (unsigned i = 0; i < stall.size(); i++) {
        if (stalled[i])
            stall[i] = uint32_t(std::min<uint64_t>(stall[i] + n,
                                                   UINT32_MAX));
    }
    stallAt = now;
}

/** 时钟沿：推进全部通道（由所属设备在 wakeAt 请求的时刻调用） */
void
HdmaEngine::tick()
{
    Tick now = clk.now();
    wakePending = MaxTick;
    accountStalls(now);
    progress = false;
    soonest = MaxTick;
    stallNow.fill(false);
    checkTags();
    if (!held) {
        for (unsigned c = 0; c < chans.size(); c++) {
            fetchDescs(c);
            startDescs(c);
            writeBack(c);
            advanceAbort(c);
            maybeErrStop(c);
            updateState(c);
        }
        issueRead();
        issueWrite();
        issueDevRead();
        issueDevWrite();
        if (pendingEvents && ml.event(pendingEvents))
            pendingEvents = 0;
    }
    stalled = stallNow;
    if (progress)
        wakeAt(now + clk.period());
    if (soonest != MaxTick)
        wakeAt(soonest);
    if (nextTagEvent != MaxTick)
        wakeAt(nextTagEvent);
}

/** 配置空间中的事务参数变化 */
void
HdmaEngine::setPcie(const PcieParams &p)
{
    pcie = p;
    pcie.mps = std::min(p.mps, 256u);
    wake();
}

/** Bus Master Enable 的当前值；由 1 变 0 时按 §8.4 停止 */
void
HdmaEngine::setBusMaster(bool en)
{
    bool was = busMaster;
    busMaster = en;
    if (!was || en)
        return;
    for (unsigned c = 0; c < chans.size(); c++) {
        for (auto &d : chans[c].descs) {
            if (d.started && !descDone(d))
                failDesc(c, d, HdmaErr::BusMasterOff, 0);
        }
    }
    wake();
}

/** 全部状态恢复复位值（tag 的隔离保留） */
void
HdmaEngine::clearState()
{
    chans.assign(cfg.channels, Channel{});
    globalEn = false;
    cplUsed = 0;
    d2hUsed = 0;
    devWrites.clear();
    pendingEvents = 0;
    rrRead = rrWrite = rrDevRead = 0;
    rdFreeAt = wrFreeAt = devWrFreeAt = devRdFreeAt = 0;
    bytesH2d = bytesD2h = 0;
    h2dHi.fill(0);
    d2hHi.fill(0);
    stall.fill(0);
    stalled.fill(false);
    stallAt = clk.now();
}

/**
 * 复位（FLR 开始、设备完全复位）：停止发出 host 请求，在途读请求的
 * tag 进入隔离，撤销 ML 上的请求，寄存器恢复复位值（§8.4）
 */
void
HdmaEngine::reset()
{
    Tick now = clk.now();
    for (unsigned t = 0; t < tags.size(); ++t) {
        auto &tg = tags[t];
        if (!tg.inUse)
            continue;
        tg.inUse = false;
        if (!tg.submitted) {
            host.cancelRead(t);
            continue;
        }
        tg.quarantined = true;
        tg.releaseAt = now + cfg.quarantine;
        host.isolateRead(t);
        tagsQuarantined++;
        nextTagEvent = std::min(nextTagEvent, tg.releaseAt);
    }
    tagsInUse = 0;
    ml.withdraw();
    gen++;
    clearState();
    if (nextTagEvent != MaxTick)
        wakeAt(nextTagEvent);
}

/** FLR 进行中保持复位：寄存器读返回复位值，写被忽略，不做任何访问 */
void
HdmaEngine::setHold(bool h)
{
    held = h;
    if (!h)
        wake();
}

/** 读通道寄存器 */
bool
HdmaEngine::readChannel(unsigned c, Addr off, uint32_t &v, bool mirror)
{
    Channel &ch = chans[c];
    switch (off) {
      case 0x00: v = ch.ctrl; return true;
      case 0x04:
        v = uint32_t(ch.state) | ((ch.head == ch.tail ? 1u : 0) << 8);
        return true;
      case 0x08: v = uint32_t(ch.ringBase); return true;
      case 0x0C: v = uint32_t(ch.ringBase >> 32); return true;
      case 0x10: v = ch.log2; return true;
      case 0x14: v = ch.tail; return true;
      case 0x18: v = ch.head; return true;
      case 0x1C: v = ch.errReg; return true;
      case 0x20: v = uint32_t(ch.errAddr); return true;
      case 0x24: v = uint32_t(ch.errAddr >> 32); return true;
      case 0x28: v = ch.descDone; return true;
      case 0x30:
        v = uint32_t(ch.bytes);
        (mirror ? ch.bytesHiMirror : ch.bytesHiFw) =
            uint32_t(ch.bytes >> 32);
        return true;
      case 0x34: v = mirror ? ch.bytesHiMirror : ch.bytesHiFw; return true;
    }
    return false;
}

/**
 * 读偏移 off 的 32 位寄存器；mirror 为 true 表示经 BAR0 的只读
 * 镜像读取（§7.4），读低位锁存高位的寄存器与固件的锁存互相独立。
 * 未实现的偏移返回 false
 */
bool
HdmaEngine::read(Addr off, uint32_t &v, bool mirror)
{
    unsigned who = mirror ? 1 : 0;
    if (off >= ChanBase) {
        unsigned c = (off - ChanBase) / ChanStride;
        if (c >= chans.size())
            return false;
        return readChannel(c, (off - ChanBase) % ChanStride, v, mirror);
    }
    switch (off) {
      case 0x000: v = cfg.version; return true;
      case 0x004: v = cfg.channels | (cfg.maxRingLog2 << 8); return true;
      case 0x008: {
        v = 0;
        for (unsigned c = 0; c < chans.size(); c++) {
            if (chans[c].state == Running)
                v |= 1u << c;
            if (chans[c].state == ErrStop)
                v |= 1u << (8 + c);
        }
        return true;
      }
      case 0x00C: v = globalEn ? 1 : 0; return true;
      case 0x010:
        v = uint32_t(bytesH2d);
        h2dHi[who] = uint32_t(bytesH2d >> 32);
        return true;
      case 0x014: v = h2dHi[who]; return true;
      case 0x018:
        v = uint32_t(bytesD2h);
        d2hHi[who] = uint32_t(bytesD2h >> 32);
        return true;
      case 0x01C: v = d2hHi[who]; return true;
      case 0x020: v = stall[StallTag]; return true;
      case 0x024: v = stall[StallD2h]; return true;
      case 0x028: v = stall[StallNoc]; return true;
      case 0x02C: v = stall[StallTx]; return true;
    }
    return false;
}

/** 写通道寄存器 */
bool
HdmaEngine::writeChannel(unsigned c, Addr off, uint32_t v)
{
    Channel &ch = chans[c];
    switch (off) {
      case 0x00:
        ch.ctrl = v & CtrlRwMask;
        if (v & CtrlAbort)
            startAbort(c);
        if ((v & CtrlResume) && ch.state == ErrStop && !ch.wbFailed &&
            !ch.aborting) {
            ch.state = Idle;
            ch.fetchIdx = ch.head;
        }
        wake();
        return true;
      case 0x08:
        if (!(ch.ctrl & CtrlEn))
            ch.ringBase = (ch.ringBase & ~0xFFFFFFFFULL) | (v & ~0x3Fu);
        return true;
      case 0x0C:
        if (!(ch.ctrl & CtrlEn))
            ch.ringBase = (ch.ringBase & 0xFFFFFFFFULL) | (uint64_t(v) << 32);
        return true;
      case 0x10: {
        unsigned l = v & 0xF;
        bool idle = ch.state == Idle && ch.descs.empty() &&
                    ch.inflight == 0 && !ch.aborting;
        if (!(ch.ctrl & CtrlEn) && idle && l >= 1 && l <= cfg.maxRingLog2) {
            ch.log2 = l;
            ch.head = ch.tail = ch.fetchIdx = 0;
        }
        return true;
      }
      case 0x14:
        if (ch.log2 == 0 || v >= entries(ch)) {
            // 超出环的范围：忽略这次写入，通道进入错误停止（§7.3）
            if (!ch.pending.valid)
                ch.pending = PendingErr{true, HdmaErr::DescInvalid,
                                        PhaseData, ch.head, 0};
            ch.stopping = true;
        } else {
            ch.tail = v;
        }
        wake();
        return true;
      case 0x04: case 0x18: case 0x1C: case 0x20: case 0x24: case 0x28:
      case 0x30: case 0x34:
        return true;  // 只读
    }
    return false;
}

/** 偏移 off 处是否是已实现的 32 位寄存器 */
bool
HdmaEngine::implemented(Addr off) const
{
    if (off % 4 != 0)
        return false;
    if (off < ChanBase)
        return off <= 0x02C;
    unsigned c = (off - ChanBase) / ChanStride;
    Addr r = (off - ChanBase) % ChanStride;
    return c < chans.size() && (r <= 0x28 || r == 0x30 || r == 0x34);
}

/** 固件写偏移 off 的 32 位寄存器；未实现的偏移返回 false */
bool
HdmaEngine::write(Addr off, uint32_t v)
{
    if (held)
        return true;  // FLR 进行中：写被忽略
    if (off >= ChanBase) {
        unsigned c = (off - ChanBase) / ChanStride;
        if (c >= chans.size())
            return false;
        return writeChannel(c, (off - ChanBase) % ChanStride, v);
    }
    switch (off) {
      case 0x00C:
        globalEn = v & 1;
        if (v & 2) {
            for (unsigned c = 0; c < chans.size(); c++)
                startAbort(c);
        }
        wake();
        return true;
      case 0x000: case 0x004: case 0x008: case 0x010: case 0x014:
      case 0x018: case 0x01C: case 0x020: case 0x024: case 0x028:
      case 0x02C:
        return true;  // 只读
    }
    return false;
}

} // namespace gw100
} // namespace gem5

namespace gem5::gw100
{

/** 在途事务、描述符与隔离资源是否已全部结算。 */
bool
HdmaEngine::quiescent() const
{
    if (tagsInUse || tagsQuarantined || cplUsed || d2hUsed ||
        !devWrites.empty() || pendingEvents) {
        return false;
    }
    for (const auto &channel : chans) {
        if (!channel.descs.empty() || channel.inflight || channel.aborting ||
            channel.abortWbPending) {
            return false;
        }
    }
    return true;
}

/** 保存已排空的寄存器、仲裁、tag LRU 与计数。 */
void
HdmaEngine::serialize(CheckpointOut &cp) const
{
    fatal_if(!quiescent(), "HDMA checkpoint requires completed work");
    SERIALIZE_SCALAR(busMaster);
    SERIALIZE_SCALAR(held);
    SERIALIZE_SCALAR(gen);
    SERIALIZE_SCALAR(nextSerial);
    SERIALIZE_SCALAR(globalEn);
    SERIALIZE_SCALAR(rrRead);
    SERIALIZE_SCALAR(rrWrite);
    SERIALIZE_SCALAR(rrDevRead);
    SERIALIZE_SCALAR(rdFreeAt);
    SERIALIZE_SCALAR(wrFreeAt);
    SERIALIZE_SCALAR(devWrFreeAt);
    SERIALIZE_SCALAR(devRdFreeAt);
    SERIALIZE_SCALAR(bytesH2d);
    SERIALIZE_SCALAR(bytesD2h);
    SERIALIZE_SCALAR(stallAt);
    SERIALIZE_CONTAINER(h2dHi);
    SERIALIZE_CONTAINER(d2hHi);
    SERIALIZE_CONTAINER(stall);
    SERIALIZE_CONTAINER(stalled);
    paramOut(cp, "pcie.mps", pcie.mps);
    paramOut(cp, "pcie.mrrs", pcie.mrrs);
    paramOut(cp, "pcie.extTag", pcie.extTag);
    paramOut(cp, "pcie.relaxed", pcie.relaxed);
    paramOut(cp, "pcie.noSnoop", pcie.noSnoop);
    paramOut(cp, "pcie.cplTimeout", pcie.cplTimeout);
    for (unsigned index = 0; index < tags.size(); ++index) {
        paramOut(cp, "tagUse" + std::to_string(index), tags[index].use);
        paramOut(cp, "tagLastUsed" + std::to_string(index),
                 tags[index].lastUsed);
    }
    for (unsigned index = 0; index < chans.size(); ++index) {
        ScopedCheckpointSection section(cp, "channel" + std::to_string(index));
        const auto &channel = chans[index];
        paramOut(cp, "ctrl", channel.ctrl);
        paramOut(cp, "ringBase", channel.ringBase);
        paramOut(cp, "log2", channel.log2);
        paramOut(cp, "tail", channel.tail);
        paramOut(cp, "head", channel.head);
        paramOut(cp, "fetchIdx", channel.fetchIdx);
        paramOut(cp, "errReg", channel.errReg);
        paramOut(cp, "errAddr", channel.errAddr);
        paramOut(cp, "descDone", channel.descDone);
        paramOut(cp, "bytes", channel.bytes);
        paramOut(cp, "bytesHiFw", channel.bytesHiFw);
        paramOut(cp, "bytesHiMirror", channel.bytesHiMirror);
        paramOut(cp, "stopping", channel.stopping);
        paramOut(cp, "wbFailed", channel.wbFailed);
        paramOut(cp, "state", unsigned(channel.state));
        arrayParamOut(cp, "credit", channel.credit);
        paramOut(cp, "pending.valid", channel.pending.valid);
        paramOut(cp, "pending.code", unsigned(channel.pending.code));
        paramOut(cp, "pending.phase", unsigned(channel.pending.phase));
        paramOut(cp, "pending.idx", channel.pending.idx);
        paramOut(cp, "pending.addr", channel.pending.addr);
    }
}

/** 恢复已排空的寄存器、仲裁、tag LRU 与计数。 */
void
HdmaEngine::unserialize(CheckpointIn &cp)
{
    clearState();
    UNSERIALIZE_SCALAR(busMaster);
    UNSERIALIZE_SCALAR(held);
    UNSERIALIZE_SCALAR(gen);
    UNSERIALIZE_SCALAR(nextSerial);
    UNSERIALIZE_SCALAR(globalEn);
    UNSERIALIZE_SCALAR(rrRead);
    UNSERIALIZE_SCALAR(rrWrite);
    UNSERIALIZE_SCALAR(rrDevRead);
    UNSERIALIZE_SCALAR(rdFreeAt);
    UNSERIALIZE_SCALAR(wrFreeAt);
    UNSERIALIZE_SCALAR(devWrFreeAt);
    UNSERIALIZE_SCALAR(devRdFreeAt);
    UNSERIALIZE_SCALAR(bytesH2d);
    UNSERIALIZE_SCALAR(bytesD2h);
    UNSERIALIZE_SCALAR(stallAt);
    arrayParamIn(cp, "h2dHi", h2dHi.data(), h2dHi.size());
    arrayParamIn(cp, "d2hHi", d2hHi.data(), d2hHi.size());
    arrayParamIn(cp, "stall", stall.data(), stall.size());
    arrayParamIn(cp, "stalled", stalled.data(), stalled.size());
    paramIn(cp, "pcie.mps", pcie.mps);
    paramIn(cp, "pcie.mrrs", pcie.mrrs);
    paramIn(cp, "pcie.extTag", pcie.extTag);
    paramIn(cp, "pcie.relaxed", pcie.relaxed);
    paramIn(cp, "pcie.noSnoop", pcie.noSnoop);
    paramIn(cp, "pcie.cplTimeout", pcie.cplTimeout);
    for (unsigned index = 0; index < chans.size(); ++index) {
        ScopedCheckpointSection section(cp, "channel" + std::to_string(index));
        auto &channel = chans[index];
        paramIn(cp, "ctrl", channel.ctrl);
        paramIn(cp, "ringBase", channel.ringBase);
        paramIn(cp, "log2", channel.log2);
        paramIn(cp, "tail", channel.tail);
        paramIn(cp, "head", channel.head);
        paramIn(cp, "fetchIdx", channel.fetchIdx);
        paramIn(cp, "errReg", channel.errReg);
        paramIn(cp, "errAddr", channel.errAddr);
        paramIn(cp, "descDone", channel.descDone);
        paramIn(cp, "bytes", channel.bytes);
        paramIn(cp, "bytesHiFw", channel.bytesHiFw);
        paramIn(cp, "bytesHiMirror", channel.bytesHiMirror);
        paramIn(cp, "stopping", channel.stopping);
        paramIn(cp, "wbFailed", channel.wbFailed);
        unsigned state, code, phase;
        paramIn(cp, "state", state);
        channel.state = State(state);
        arrayParamIn(cp, "credit", channel.credit.data(),
                     channel.credit.size());
        paramIn(cp, "pending.valid", channel.pending.valid);
        paramIn(cp, "pending.code", code);
        paramIn(cp, "pending.phase", phase);
        channel.pending.code = HdmaErr(code);
        channel.pending.phase = Phase(phase);
        paramIn(cp, "pending.idx", channel.pending.idx);
        paramIn(cp, "pending.addr", channel.pending.addr);
    }
    for (unsigned index = 0; index < tags.size(); ++index) {
        paramIn(cp, "tagUse" + std::to_string(index), tags[index].use);
        paramIn(cp, "tagLastUsed" + std::to_string(index),
                tags[index].lastUsed);
    }
    tagsInUse = tagsQuarantined = 0;
    nextTagEvent = MaxTick;
    wakePending = MaxTick;
}

} // namespace gem5::gw100
