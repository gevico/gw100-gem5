/**
 * @file  link_transport.cc
 * @brief PCIe 请求者账本、完整 TLP 目标服务与完成拆包的实现
 *
 * @see spec-pcie, spec-host-dma
 */

#include "src/gw100/pcie/link/link_transport.hh"

#include <algorithm>
#include <stdexcept>

namespace gem5::gw100::pcie
{

/** 构造共享双向链路，start() 由所属设备启动时调用。 */
LinkTransport::LinkTransport(LinkClockIf &clock, const LinkConfig &config,
                             uint16_t endpoint_id, Tick timeout,
                             Tick quarantine, unsigned rcb,
                             unsigned completion_bytes,
                             const LinkServices &services,
                             unsigned crs_retries)
    : clock(clock),
      services(services),
      link(clock, *this, config),
      endpointId(endpoint_id),
      timeout(timeout),
      quarantine(quarantine),
      crsRetries(crs_retries),
      rcb(rcb),
      completionBytes(completion_bytes)
{
    if ((rcb != 64 && rcb != 128) ||
        (completion_bytes != 64 && completion_bytes != 128 &&
         completion_bytes != 256) ||
        completion_bytes < rcb || !timeout || !quarantine ||
        !services.execute) {
        throw std::invalid_argument("PCIe transport configuration");
    }
}

/** 指定侧仍等待 completion 的读，包括隔离元数据。 */
unsigned
LinkTransport::outstanding(unsigned side) const
{
    return reads.at(side).size();
}

/** 已提交且尚未收到完成或超时的请求，对应 Transactions Pending。 */
unsigned
LinkTransport::pendingReads(unsigned side) const
{
    unsigned count = 0;
    for (const auto &[key, read] : reads.at(side)) {
        count += read.submitted && !read.isolated;
    }
    return count;
}

/** 当前目标之前的 posted 操作均已对后续读取可见。 */
bool
LinkTransport::precedingPostedVisible(unsigned side, uint64_t cookie) const
{
    for (const auto &target : targets.at(side)) {
        if (target->request.cookie == cookie) {
            return true;
        }
        if (target->request.traffic() == Traffic::Posted && !target->ready) {
            return false;
        }
    }
    return true;
}

/** 为指定侧保留完整 non-posted 请求并提交。 */
bool
LinkTransport::submitRead(unsigned side, Tlp &request,
                          std::function<void()> committed, HostReadDone done)
{
    Key key{request.requester, request.tag};
    unsigned bytes = request.length * 4;
    unsigned headers = (request.requestBytes() +
                        (request.address + request.firstOffset()) % 64 + 63) /
                       64;
    if (!link.canSubmit(side, request) || reads[side].count(key) ||
        reads[side].size() >= 256 ||
        reserved[side] + bytes > pciedefs::COMPLETION_DATA_BYTES ||
        headerReservations[side] + headers >
            pciedefs::COMPLETION_HEADER_SLOTS) {
        return false;
    }
    unsigned active = 0;
    for (const auto &[key, read] : reads[side]) {
        active += !read.isolated;
    }
    if (active >= pciedefs::MAX_OUTSTANDING_READS) {
        return false;
    }
    if (!link.submit(side, request)) {
        return false;
    }
    Read read;
    read.request = request;
    read.data.resize(request.requestBytes());
    read.reserved = bytes;
    read.headers = headers;
    read.committed = std::move(committed);
    read.done = std::move(done);
    reads[side].emplace(key, std::move(read));
    reserved[side] += bytes;
    headerReservations[side] += headers;
    return true;
}

/** Host PIO 的本地准入，posted 写调用者在接受后结算 CPU 响应。 */
bool
LinkTransport::submitPio(Tlp &request, HostReadDone done)
{
    request.requester = 0;
    if (request.traffic() == Traffic::Posted) {
        return link.submit(0, request);
    }
    for (unsigned offset = 0; offset < 256; ++offset) {
        unsigned tag = (nextTag + offset) & 255;
        if (reads[0].count({0, tag})) {
            continue;
        }
        request.tag = tag;
        if (!submitRead(0, request, {}, std::move(done))) {
            return false;
        }
        nextTag = (tag + 1) & 255;
        return true;
    }
    return false;
}

/** 旧调用缺少真实 tag，精确路径要求使用 readTagged。 */
void
LinkTransport::read(Addr, unsigned, HostReadDone)
{
    throw std::logic_error("PCIe precise read requires HDMA wire tag");
}

/** 指定完整 MRd 当前能否被本地发送端接受。 */
bool
LinkTransport::canRead(Addr addr, unsigned len) const
{
    if (!enabled || !len || len > 512 ||
        reserved[1] + ((len + (addr & 3) + 3) & ~3u) >
            pciedefs::COMPLETION_DATA_BYTES) {
        return false;
    }
    auto request = Tlp::request(TlpKind::MemoryRead, addr, len, endpointId, 0);
    return link.canSubmit(1, request);
}

/** 接受真实 tag 与 Attr[2:0]，在 DLL 提交时调用 committed。 */
bool
LinkTransport::readTagged(Addr addr, unsigned len, uint8_t tag,
                          std::function<void()> committed, HostReadDone done,
                          uint8_t attributes)
{
    if (!canRead(addr, len)) {
        return false;
    }
    auto request =
        Tlp::request(TlpKind::MemoryRead, addr, len, endpointId, tag);
    request.attributes = attributes;
    return submitRead(1, request, std::move(committed), std::move(done));
}

/** 发送通路能否再接受一个写请求。 */
bool
LinkTransport::canWrite() const
{
    if (!enabled) {
        return false;
    }
    Tlp request;
    request.kind = TlpKind::MemoryWrite;
    request.payload.resize(mps);
    return link.canSubmit(1, request);
}

/** 发出一个 MWr，载荷归共享有限发送队列所有。 */
void
LinkTransport::write(Addr addr, unsigned len, const uint8_t *data,
                     uint8_t attributes)
{
    auto request =
        Tlp::request(TlpKind::MemoryWrite, addr, len, endpointId, 0, data);
    request.attributes = attributes;
    if (!enabled || !link.submit(1, request)) {
        throw std::logic_error("PCIe write without local capacity");
    }
    postedBeforeCommit.insert(request.cookie);
}

/** 释放一次完成预留。 */
void
LinkTransport::unreserve(unsigned side, Read &read)
{
    reserved[side] -= read.reserved;
    headerReservations[side] -= read.headers;
    read.reserved = 0;
    read.headers = 0;
}

/** 结算请求者，先释放本地资源再调用上游。 */
void
LinkTransport::finish(unsigned side, const Key &key, HostStatus status)
{
    auto found = reads[side].find(key);
    if (found == reads[side].end()) {
        return;
    }
    auto read = std::move(found->second);
    unreserve(side, read);
    reads[side].erase(found);
    if (read.done) {
        read.done(status, read.data.data());
    }
    if (services.ready) {
        services.ready();
    }
}

/** 撤销尚未提交到 DLL 的读，释放本地预留。 */
void
LinkTransport::cancelRead(uint8_t tag)
{
    auto found = reads[1].find({endpointId, tag});
    if (found != reads[1].end() && !found->second.submitted) {
        link.cancel(1, found->second.request.cookie);
    }
}

/** 已提交读进入隔离，保留匹配元数据并释放数据预留。 */
void
LinkTransport::isolateRead(uint8_t tag)
{
    auto found = reads[1].find({endpointId, tag});
    if (found == reads[1].end() || found->second.isolated) {
        return;
    }
    auto &read = found->second;
    isolate(1, read);
}

/** 停止上游通知并保留真实 tag，直到迟到完成或隔离期限。 */
void
LinkTransport::isolate(unsigned side, Read &read)
{
    read.isolated = true;
    read.deadline = clock.now() + quarantine;
    unreserve(side, read);
    read.data.clear();
    read.done = {};
    link.noteRetired(side, read.request.cookie);
    clock.wakeAt(read.deadline);
}

/** 复位或电源门控撤销尚未提交的主动业务。 */
void
LinkTransport::cancelPending()
{
    std::vector<uint8_t> tags;
    for (const auto &[key, read] : reads[1]) {
        if (!read.submitted) {
            tags.push_back(key.second);
        }
    }
    for (auto tag : tags) {
        cancelRead(tag);
    }
    auto posted = postedBeforeCommit;
    for (auto cookie : posted) {
        link.cancel(1, cookie);
    }
}

/** INTx 逻辑电平变化，有限状态寄存器等待共同发送队列。 */
void
LinkTransport::setIntx(bool asserted)
{
    intxDesired = asserted;
    clock.wakeAt(clock.now());
}

/** 功能门控结束后允许主动请求，配置与完成独立服务。 */
void
LinkTransport::setActive(bool allowed)
{
    enabled = allowed;
    if (services.ready) {
        services.ready();
    }
}

/** 有限错误状态锁存器经共同有序队列发送 AER Message。 */
void
LinkTransport::sendErrorMessage(uint8_t code)
{
    if (code != 0x30 && code != 0x31 && code != 0x33) {
        throw std::invalid_argument("PCIe error message code");
    }
    errorMessages |= code == 0x30 ? 1 : code == 0x31 ? 2 : 4;
    clock.wakeAt(clock.now());
}

/** 更新后续事务的 MPS 和 Extended Synch。 */
bool
LinkTransport::configure(unsigned payload, bool extended_synch)
{
    if (!link.configure(0, payload, extended_synch) ||
        !link.configure(1, payload, extended_synch)) {
        return false;
    }
    mps = payload;
    return true;
}

/** 在有限目标执行槽中接收完整 TLP。 */
bool
LinkTransport::receive(unsigned side, const Tlp &tlp,
                       std::function<void()> release)
{
    if (tlp.kind == TlpKind::Completion) {
        completion(side, tlp);
        release();
        return true;
    }
    if (tlp.kind == TlpKind::Message) {
        for (const auto &target : targets[side]) {
            if (target->request.traffic() == Traffic::Posted &&
                !target->ready) {
                return false;
            }
        }
        if (!side && (tlp.messageCode == 0x20 || tlp.messageCode == 0x24)) {
            if (services.intx) {
                services.intx(tlp.messageCode == 0x20);
            }
        }
        release();
        return true;
    }
    if (tlp.traffic() == Traffic::NonPosted) {
        auto [entry, inserted] =
            postedDependencies[side].try_emplace(tlp.cookie);
        if (inserted) {
            for (const auto &target : targets[side]) {
                if (target->request.traffic() == Traffic::Posted &&
                    !target->ready) {
                    entry->second.push_back(target);
                }
            }
        }
        for (const auto &dependency : entry->second) {
            auto target = dependency.lock();
            if (target && !target->ready) {
                return false;
            }
        }
        auto released = std::move(release);
        release = [this, side, cookie = tlp.cookie, released] {
            postedDependencies[side].erase(cookie);
            released();
        };
    }
    if (targets[side].size() == pciedefs::BACKEND_SLOTS) {
        return false;
    }
    if (side == 0 && tlp.traffic() == Traffic::NonPosted) {
        unsigned non_posted = std::count_if(
            targets[side].begin(), targets[side].end(),
            [](const auto &target) {
                return target->request.traffic() == Traffic::NonPosted;
            });
        if (non_posted >=
            pciedefs::BACKEND_SLOTS - pciedefs::RC_POSTED_RESERVED_SLOTS) {
            return false;
        }
    }
    bool zero_length = tlp.isRequest() && tlp.length == 1 && !tlp.firstBe;
    auto target = std::make_shared<Target>();
    target->request = tlp;
    target->generation = generation;
    targets[side].push_back(target);
    if (tlp.poisoned && !tlp.payload.empty()) {
        link.noteError(side, LinkError::Poisoned, tlp.encode());
        target->status = CompletionStatus::Ur;
        target->ready = true;
        release();
        clock.wakeAt(clock.now());
        return true;
    }
    if (tlp.kind == TlpKind::Unsupported) {
        link.noteError(side, LinkError::UnsupportedRequest, tlp.encode());
        target->status = CompletionStatus::Ur;
        target->ready = true;
        release();
        clock.wakeAt(clock.now());
        return true;
    }
    if (zero_length) {
        target->ready = true;
        if (tlp.kind == TlpKind::MemoryRead ||
            tlp.kind == TlpKind::ConfigRead) {
            target->data.resize(4);
        } else if (tlp.traffic() == Traffic::Posted) {
            link.noteVisible(side, tlp.cookie);
        }
        release();
        clock.wakeAt(clock.now());
        return true;
    }
    bool accepted = services.execute(
        side, tlp,
        [this, side, target](CompletionStatus status,
                             const std::vector<uint8_t> &data) {
            target->ready = true;
            target->status = status;
            target->data = data;
            if (status != CompletionStatus::Success &&
                status != CompletionStatus::Crs) {
                link.noteError(side,
                               status == CompletionStatus::Ca
                                   ? LinkError::CompleterAbort
                                   : LinkError::UnsupportedRequest,
                               target->request.encode());
            }
            if (status == CompletionStatus::Success &&
                target->request.traffic() == Traffic::Posted &&
                target->generation == generation) {
                link.noteVisible(side, target->request.cookie);
            }
            clock.wakeAt(clock.now());
        });
    if (!accepted) {
        targets[side].pop_back();
        return false;
    }
    release();
    return true;
}

/** 接收正常 completion 或可明确报告的错误。 */
void
LinkTransport::completion(unsigned side, const Tlp &tlp)
{
    Key key{tlp.requester, tlp.tag};
    auto found = reads[side].find(key);
    if (found == reads[side].end()) {
        link.noteError(side, LinkError::UnexpectedCompletion, tlp.encode());
        return;
    }
    auto &read = found->second;
    if (tlp.status == CompletionStatus::Crs && !tlp.poisoned) {
        if (read.isolated) {
            return;
        }
        if (!side && read.request.isConfig() && tlp.payload.empty() &&
            !read.received && read.retries < crsRetries) {
            link.noteComplete(side, read.request.cookie);
            ++read.retries;
            read.request.cookie = 0;
            read.submitted = false;
            read.deadline = UINT64_MAX;
            read.retryAt = clock.now() + pciedefs::RC_CRS_RETRY_PS;
            clock.wakeAt(read.retryAt);
            return;
        }
        link.noteError(side, LinkError::CompleterAbort, tlp.encode());
        link.noteComplete(side, read.request.cookie);
        finish(side, key, HostStatus::Ca);
        return;
    }
    HostStatus status = HostStatus::Ok;
    if (tlp.poisoned) {
        status = HostStatus::Poisoned;
        link.noteError(side, LinkError::Poisoned, tlp.encode());
    } else if (tlp.status != CompletionStatus::Success) {
        status = tlp.status == CompletionStatus::Ca ? HostStatus::Ca
                                                    : HostStatus::Ur;
        link.noteError(side,
                       tlp.status == CompletionStatus::Ca
                           ? LinkError::CompleterAbort
                           : LinkError::UnsupportedRequest,
                       tlp.encode());
    }
    bool config = read.request.isConfig();
    bool write = read.request.kind == TlpKind::ConfigWrite;
    uint64_t address =
        read.request.address + read.request.firstOffset() + read.received;
    unsigned remaining = read.request.requestBytes() - read.received;
    unsigned prefix = config ? read.request.firstOffset() : address & 3;
    unsigned bytes =
        tlp.payload.size() > prefix
            ? std::min<unsigned>(remaining, tlp.payload.size() - prefix)
            : 0;
    bool zero_length = read.request.length == 1 && !read.request.firstBe;
    unsigned byte_count = config ? 4 : zero_length ? 1 : remaining;
    if (status == HostStatus::Ok &&
        (tlp.bcm || tlp.tc != read.request.tc || tlp.byteCount != byte_count ||
         tlp.lowerAddress != (config ? 0 : address & 127) ||
         tlp.payload.size() > ((prefix + remaining + 3) & ~3u) ||
         (write ? !tlp.payload.empty() : !bytes) ||
         (!write && bytes < remaining &&
          ((address + bytes) % rcb ||
           tlp.payload.size() != tlp.length * 4)))) {
        link.noteError(side, LinkError::Malformed, tlp.encode());
        status = HostStatus::Ca;
    }
    if (status != HostStatus::Ok) {
        if (read.isolated) {
            return;
        }
        link.noteComplete(side, read.request.cookie);
        finish(side, key, status);
        return;
    }
    if (!read.isolated && bytes) {
        std::copy_n(tlp.payload.begin() + prefix, bytes,
                    read.data.begin() + read.received);
    }
    read.received += bytes;
    if (write || read.received == read.request.requestBytes()) {
        if (!read.isolated) {
            link.noteComplete(side, read.request.cookie);
        }
        finish(side, key, HostStatus::Ok);
    }
}

/** 将目标结果逐包提交到共享链路。 */
void
LinkTransport::pumpTargets()
{
    for (unsigned side = 0; side < 2; ++side) {
        auto &queue = targets[side];
        for (auto entry = queue.begin(); entry != queue.end();) {
            auto target = *entry;
            if (!target->ready) {
                ++entry;
                continue;
            }
            if (target->generation != generation ||
                target->request.traffic() == Traffic::Posted) {
                entry = queue.erase(entry);
                link.retryReceive();
                continue;
            }
            const auto &request = target->request;
            unsigned remaining = target->data.size() - target->offset;
            uint64_t address =
                request.address + request.firstOffset() + target->offset;
            unsigned bytes = std::min({remaining, completionBytes, mps});
            // 非最后片段必须结束于允许的 RCB 边界，首部禁用字节计入 MPS。
            if (bytes + (address & 3) > mps) {
                bytes -= address & 3;
            }
            if (bytes < remaining) {
                bytes -= (address + bytes) % rcb;
            }
            if (remaining && !bytes) {
                throw std::logic_error("PCIe completion cannot satisfy RCB");
            }
            std::vector<uint8_t> data(target->data.begin() + target->offset,
                                      target->data.begin() + target->offset +
                                          bytes);
            auto completion =
                Tlp::completion(request, side ? endpointId : 0, target->offset,
                                data, target->status);
            if (!link.submit(side, completion)) {
                ++entry;
                continue;
            }
            target->offset += bytes;
            if (target->offset == target->data.size()) {
                entry = queue.erase(entry);
                link.retryReceive();
            }
        }
    }
    if (intxDesired != intxSubmitted) {
        Tlp message;
        message.kind = TlpKind::Message;
        message.requester = endpointId;
        message.messageCode = intxDesired ? 0x20 : 0x24;
        message.messageRouting = 0;
        if (link.submit(1, message)) {
            intxSubmitted = intxDesired;
        }
    }
    for (unsigned bit = 0; bit < 3; ++bit) {
        if (!(errorMessages & (1u << bit))) {
            continue;
        }
        Tlp message;
        message.kind = TlpKind::Message;
        message.requester = endpointId;
        message.messageCode = bit == 2 ? 0x33 : 0x30 + bit;
        message.messageRouting = 0;
        if (link.submit(1, message)) {
            errorMessages &= ~(1u << bit);
        }
    }
}

/** 检查 RC 完成超时和 EP/RC 隔离元数据期限。 */
void
LinkTransport::checkDeadlines()
{
    for (unsigned side = 0; side < 2; ++side) {
        for (auto entry = reads[side].begin(); entry != reads[side].end();) {
            auto &read = entry->second;
            if (read.retryAt != UINT64_MAX) {
                if (read.retryAt > clock.now()) {
                    clock.wakeAt(read.retryAt);
                } else if (link.submit(side, read.request)) {
                    read.retryAt = UINT64_MAX;
                }
                ++entry;
                continue;
            }
            if (read.deadline > clock.now()) {
                if (read.deadline != UINT64_MAX) {
                    clock.wakeAt(read.deadline);
                }
                ++entry;
                continue;
            }
            if (read.isolated) {
                entry = reads[side].erase(entry);
                if (services.ready) {
                    services.ready();
                }
                continue;
            }
            // EP 完成超时由 HDMA 启动和结算，RC 在本地结算其 PIO。
            ++entry;
            link.noteError(side, LinkError::CompletionTimeout,
                           read.request.encode());
            auto done = std::move(read.done);
            isolate(side, read);
            if (done) {
                done(HostStatus::Ca, nullptr);
            }
        }
    }
}

/** 推进链路、目标结果及隔离期限。 */
void
LinkTransport::tick()
{
    link.tick();
    pumpTargets();
    checkDeadlines();
}

/** 源端 TLP 已进入数据链路层。 */
void
LinkTransport::committed(unsigned side, uint64_t cookie)
{
    if (side) {
        postedBeforeCommit.erase(cookie);
    }
    for (auto &[key, read] : reads[side]) {
        if (read.request.cookie != cookie) {
            continue;
        }
        read.submitted = true;
        if (!side) {
            read.deadline = clock.now() + timeout;
            clock.wakeAt(read.deadline);
        }
        if (read.committed) {
            read.committed();
        }
        return;
    }
}

/** 发送资源变化，可重试本地准入。 */
void
LinkTransport::transmitReady(unsigned)
{
    clock.wakeAt(clock.now());
    if (services.ready) {
        services.ready();
    }
}

/** 物理复位作废本地事务，submitted 指该 TLP 曾经进入 DLL。 */
void
LinkTransport::cancelled(unsigned side, uint64_t cookie, bool)
{
    postedBeforeCommit.erase(cookie);
    for (auto entry = reads[side].begin(); entry != reads[side].end();
         ++entry) {
        if (entry->second.request.cookie != cookie) {
            continue;
        }
        unreserve(side, entry->second);
        reads[side].erase(entry);
        break;
    }
}

/** 错误诊断，header 可为空。 */
void
LinkTransport::error(unsigned side, LinkError kind,
                     const std::vector<uint8_t> &header)
{
    if (services.error) {
        services.error(side, kind, header);
    }
}

/** 链路状态、速率或 DLL 就绪状态变化。 */
void
LinkTransport::stateChanged()
{
    if (link.state() == LinkState::Down) {
        ++generation;
        postedBeforeCommit.clear();
        for (unsigned side = 0; side < 2; ++side) {
            postedDependencies[side].clear();
            while (!reads[side].empty()) {
                finish(side, reads[side].begin()->first, HostStatus::Ca);
            }
        }
        intxSubmitted = false;
        if (services.intx) {
            services.intx(false);
        }
    }
    if (services.stateChanged) {
        services.stateChanged();
    }
    if (services.ready) {
        services.ready();
    }
}

/** 请求者与目标后端是否已经排空。 */
bool
LinkTransport::quiescent() const
{
    return reads[0].empty() && reads[1].empty() && targets[0].empty() &&
           targets[1].empty() && postedDependencies[0].empty() &&
           postedDependencies[1].empty() && postedBeforeCommit.empty() &&
           intxDesired == intxSubmitted && !errorMessages;
}

/** 可选的事件级观测。 */
void
LinkTransport::observe(const LinkTrace &trace)
{
    if (services.observe) {
        services.observe(trace);
    }
}

/** 排空完成时通知所属 SimObject。 */
void
LinkTransport::drained()
{
    if (services.drained) {
        services.drained();
    }
}

} // namespace gem5::gw100::pcie

namespace gem5::gw100::pcie
{

/** 保存没有在途事务的完整链路与事务层状态。 */
TransportSnapshot
LinkTransport::snapshot() const
{
    if (!quiescent()) {
        throw std::logic_error("PCIe transport checkpoint has pending work");
    }
    return {link.snapshot(), endpointId, enabled, intxSubmitted, nextTag};
}

/** 恢复全部排空状态，源和目标由 drainResume 继续运行。 */
void
LinkTransport::restore(const TransportSnapshot &saved)
{
    if (saved.endpointId != endpointId || saved.nextTag > 255 ||
        !quiescent()) {
        throw std::invalid_argument("PCIe transport checkpoint topology");
    }
    link.restore(saved.link);
    mps = saved.link.ports[1].mps;
    enabled = saved.enabled;
    nextTag = saved.nextTag;
    intxDesired = saved.intx;
    intxSubmitted = saved.intx;
    errorMessages = 0;
    if (services.intx) {
        services.intx(saved.intx);
    }
}

} // namespace gem5::gw100::pcie
