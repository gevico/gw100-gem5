/**
 * @file  replay.hh
 * @brief PCIe 有限重放窗口与累计确认
 *
 * 数据按实际保留包分配，描述符预算单独检查。重传者共享不可变副本。
 *
 * @see spec-pcie
 */

#ifndef GW100_PCIE_LINK_REPLAY_HH
#define GW100_PCIE_LINK_REPLAY_HH

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "src/gw100/pcie/link/packet.hh"

namespace gem5::gw100::pcie
{

/** 有限 replay 数据与描述符窗口。 */
class ReplayWindow
{
  public:
    /** 一个不可变的重传副本。 */
    struct Entry
    {
        /** 本地观测编号。 */
        uint64_t cookie = 0;
        /** 原始十二位序号。 */
        uint16_t sequence = 0;
        /** 实际存储分配量，单位 B。 */
        unsigned allocated = 0;
        /** 保留位、序号、TLP 和 LCRC 的原始副本。 */
        std::vector<uint8_t> packet;
    };
    /** 累计确认的结果。 */
    struct AckResult
    {
        /** 序号合法，含等于上次确认值的重复 ACK。 */
        bool valid = false;
        /** 本次确认释放了副本。 */
        bool progress = false;
        /** 本次释放的本地观测编号。 */
        std::vector<uint64_t> cookies;
    };

    /** 以独立的数据、描述符和对齐预算构造。 */
    ReplayWindow(unsigned data_bytes, unsigned descriptors,
                 unsigned alignment);
    /** DL_Down 后清空窗口并初始化序号。 */
    void reset();
    /** 头和载荷为 body_bytes 的 TLP 是否可进入数据链路层。 */
    bool canAppend(unsigned body_bytes) const;
    /** 分配序号并保留原始副本，资源不足时返回空。 */
    std::shared_ptr<const Entry> append(const Tlp &tlp);
    /** 接收累计 ACK 或 NAK 中的已确认序号。 */
    AckResult acknowledge(uint16_t sequence);
    /** 查找仍未确认的副本。 */
    std::shared_ptr<const Entry> find(uint16_t sequence) const;
    /** 读取按原始发送顺序排列的副本。 */
    const std::deque<std::shared_ptr<const Entry>> &
    entries() const
    {
        return pending;
    }
    /** 已占用的数据存储，单位 B。 */
    unsigned
    usedBytes() const
    {
        return used;
    }
    /** 已占用的描述符数。 */
    unsigned
    size() const
    {
        return pending.size();
    }
    /** 下一次提交分配的序号。 */
    uint16_t
    nextSequence() const
    {
        return next;
    }
    /** 最近累计确认的序号。 */
    uint16_t
    acknowledgedSequence() const
    {
        return acked;
    }
    /** 恢复已排空窗口的序号状态。 */
    void restoreSequence(uint16_t next_sequence, uint16_t acknowledged);

  private:
    /** 计算保留位、序号、TLP、LCRC 的对齐存储量，单位 B。 */
    uint64_t allocation(unsigned body_bytes) const;
    /** replay 数据容量，单位 B。 */
    const unsigned capacity;
    /** replay 描述符数上限。 */
    const unsigned descriptorLimit;
    /** 存储分配对齐，单位 B。 */
    const unsigned alignment;
    /** 已占用的数据存储，单位 B。 */
    unsigned used = 0;
    /** 下一次发送的十二位序号。 */
    uint16_t next = 0;
    /** 最近确认序号，上电为 4095。 */
    uint16_t acked = 4095;
    /** 按原始发送顺序保存的有限副本。 */
    std::deque<std::shared_ptr<const Entry>> pending;
};

} // namespace gem5::gw100::pcie
#endif
