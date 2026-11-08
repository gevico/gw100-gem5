/**
 * @file  replay.cc
 * @brief PCIe 有限重放窗口的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/link/replay.hh"

#include <algorithm>
#include <stdexcept>

namespace gem5::gw100::pcie
{

/** 以独立的数据、描述符和对齐预算构造。 */
ReplayWindow::ReplayWindow(unsigned data_bytes, unsigned descriptors,
                           unsigned alignment)
    : capacity(data_bytes), descriptorLimit(descriptors), alignment(alignment)
{
    if (!capacity || !descriptorLimit || !alignment ||
        (alignment & (alignment - 1))) {
        throw std::invalid_argument("PCIe replay budget");
    }
}

/** DL_Down 后清空窗口并初始化序号。 */
void
ReplayWindow::reset()
{
    pending.clear();
    used = 0;
    next = 0;
    acked = 4095;
}

/** 计算保留位、序号、TLP、LCRC 的对齐存储量，单位 B。 */
uint64_t
ReplayWindow::allocation(unsigned body_bytes) const
{
    return (uint64_t(body_bytes) + 6 + alignment - 1) &
           ~uint64_t(alignment - 1);
}

/** 头和载荷为 body_bytes 的 TLP 是否可进入数据链路层。 */
bool
ReplayWindow::canAppend(unsigned body_bytes) const
{
    return body_bytes >= 12 && pending.size() < descriptorLimit &&
           allocation(body_bytes) <= capacity - used &&
           ((next - acked) & 4095) < 2048;
}

/** 分配序号并保留原始副本，资源不足时返回空。 */
std::shared_ptr<const ReplayWindow::Entry>
ReplayWindow::append(const Tlp &tlp)
{
    unsigned body_bytes = tlp.headerBytes() + tlp.payload.size();
    if (!canAppend(body_bytes)) {
        return {};
    }
    auto entry = std::make_shared<Entry>();
    entry->cookie = tlp.cookie;
    entry->sequence = next;
    entry->packet = dataLinkPacket(tlp, next);
    entry->allocated = allocation(body_bytes);
    used += entry->allocated;
    next = (next + 1) & 4095;
    pending.push_back(entry);
    return entry;
}

/** 接收累计 ACK 或 NAK 中的已确认序号。 */
ReplayWindow::AckResult
ReplayWindow::acknowledge(uint16_t sequence)
{
    AckResult result;
    if (sequence == acked) {
        result.valid = true;
        return result;
    }
    auto end = std::find_if(
        pending.begin(), pending.end(),
        [sequence](const auto &entry) { return entry->sequence == sequence; });
    if (end == pending.end()) {
        return result;
    }
    result.valid = true;
    result.progress = true;
    size_t count = std::distance(pending.begin(), end) + 1;
    while (count--) {
        auto entry = pending.front();
        result.cookies.push_back(entry->cookie);
        used -= entry->allocated;
        pending.pop_front();
    }
    acked = sequence;
    return result;
}

/** 查找仍未确认的副本。 */
std::shared_ptr<const ReplayWindow::Entry>
ReplayWindow::find(uint16_t sequence) const
{
    auto found = std::find_if(
        pending.begin(), pending.end(),
        [sequence](const auto &entry) { return entry->sequence == sequence; });
    if (found == pending.end()) {
        return {};
    }
    return *found;
}

/** 恢复已排空窗口的序号状态。 */
void
ReplayWindow::restoreSequence(uint16_t next_sequence, uint16_t acknowledged)
{
    if (!pending.empty() || next_sequence >= 4096 || acknowledged >= 4096 ||
        next_sequence != ((acknowledged + 1) & 4095)) {
        throw std::invalid_argument("PCIe replay checkpoint sequence");
    }
    next = next_sequence;
    acked = acknowledged;
}

} // namespace gem5::gw100::pcie
