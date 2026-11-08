/**
 * @file  link_resources.test.cc
 * @brief 验证信用和 replay 的有限容量、回绕与确认规则
 *
 * @see spec-pcie
 */

#include <gtest/gtest.h>

#include "src/gw100/pcie/link/flow_control.hh"
#include "src/gw100/pcie/link/replay.hh"

using namespace gem5::gw100::pcie;

/** 取得 InitFC 广告之前，发送器没有可用信用。 */
TEST(LinkCredits, InitializationAndExhaustion)
{
    CreditCounter header(8);
    EXPECT_FALSE(header.canSpend(1));
    EXPECT_TRUE(header.initialize(32, 1));
    EXPECT_FALSE(header.initialize(32, 1));
    EXPECT_EQ(header.available(), 32);
    header.spend(32);
    EXPECT_FALSE(header.canSpend(1));
    EXPECT_THROW(header.spend(1), std::logic_error);
    EXPECT_TRUE(header.update(33, 1));
    EXPECT_EQ(header.available(), 1);
}

/** 零值只在初始化时声明无限，UpdateFC 中的零可为正常累计回绕。 */
TEST(LinkCredits, InfiniteAdvertisement)
{
    CreditCounter data(12);
    EXPECT_TRUE(data.initialize(0, 1));
    EXPECT_EQ(data.available(), UINT32_MAX);
    data.spend(10000);
    EXPECT_TRUE(data.update(0, 1));
    EXPECT_FALSE(data.update(1, 1));
}

/** 有限头部计数跨过多次八位回绕仍保持相同实体额度。 */
TEST(LinkCredits, HeaderWrap)
{
    CreditCounter header(8);
    header.initialize(32, 1);
    for (unsigned count = 1; count <= 10000; ++count) {
        header.spend(1);
        EXPECT_TRUE(header.update((32 + count) & 255, 1));
        EXPECT_EQ(header.available(), 32);
    }
}

/** 数据计数跨过十二位回绕，单位仍为 16 B。 */
TEST(LinkCredits, DataWrap)
{
    CreditCounter data(12);
    data.initialize(128, 1);
    for (unsigned count = 1; count <= 10000; ++count) {
        data.spend(16);
        EXPECT_TRUE(data.update((128 + count * 16) & 4095, 1));
        EXPECT_EQ(data.available(), 128);
    }
}

/** 四种协议尺度编码均使用原始 FC unit 判断可发送性。 */
TEST(LinkCredits, Scales)
{
    for (unsigned scale = 0; scale < 4; ++scale) {
        unsigned shift = scale < 2 ? 0 : (scale - 1) * 2;
        CreditCounter header(8);
        ASSERT_TRUE(header.initialize(2, scale));
        EXPECT_EQ(header.available(), 2u << shift);
        header.spend(2u << shift);
        EXPECT_FALSE(header.canSpend(1));
        EXPECT_TRUE(header.update(3, scale));
        EXPECT_EQ(header.available(), 1u << shift);
    }
}

/** 变更已协商尺度、倒退字段和超过半模数的广告均被拒绝。 */
TEST(LinkCredits, InvalidUpdates)
{
    CreditCounter header(8);
    EXPECT_FALSE(header.initialize(128, 1));
    EXPECT_FALSE(header.initialize(32, 4));
    ASSERT_TRUE(header.initialize(32, 1));
    EXPECT_FALSE(header.update(33, 2));
    EXPECT_FALSE(header.update(31, 1));
    EXPECT_FALSE(header.update(160, 1));
    EXPECT_EQ(header.available(), 32);
}

/** 已排空的累计计数可以保存并继续跨回绕使用。 */
TEST(LinkCredits, Checkpoint)
{
    CreditCounter original(8);
    original.initialize(32, 1);
    original.spend(16);
    original.update(48, 1);
    CreditCounter restored(8);
    restored.restore(original.state());
    EXPECT_EQ(restored.available(), original.available());
    EXPECT_EQ(restored.state().consumed, 16);
}

/** 一个 12 B 无载荷 TLP 实际保留 18 B，对齐后占 32 B。 */
TEST(LinkReplay, ActualStoredBytes)
{
    ReplayWindow window(4096, 128, 16);
    auto tlp = Tlp::request(TlpKind::MemoryRead, 0x1000, 64, 1, 2);
    tlp.cookie = 123;
    auto entry = window.append(tlp);
    ASSERT_TRUE(entry);
    EXPECT_EQ(entry->packet.size(), 18);
    EXPECT_EQ(entry->allocated, 32);
    EXPECT_EQ(window.usedBytes(), 32);
    EXPECT_EQ(entry->cookie, 123);
}

/** 4 KiB 数据预算在大量最大载荷写入下先耗尽。 */
TEST(LinkReplay, DataCapacity)
{
    ReplayWindow window(4096, 128, 16);
    uint8_t data[256] = {};
    auto tlp = Tlp::request(TlpKind::MemoryWrite, 0x1000, 256, 1, 0, data);
    unsigned count = 0;
    while (window.append(tlp)) {
        ++count;
    }
    EXPECT_EQ(count, 14);
    EXPECT_EQ(window.usedBytes(), 14 * 288);
    EXPECT_LE(window.usedBytes(), 4096);
}

/** 元数据预算独立于数据预算生效。 */
TEST(LinkReplay, DescriptorCapacity)
{
    ReplayWindow window(4096, 2, 16);
    auto tlp = Tlp::request(TlpKind::MemoryRead, 0x1000, 64, 1, 2);
    EXPECT_TRUE(window.append(tlp));
    EXPECT_TRUE(window.append(tlp));
    EXPECT_FALSE(window.append(tlp));
    EXPECT_EQ(window.usedBytes(), 64);
}

/** 累计 ACK 释放整段前缀，重复 ACK 合法且不产生前进。 */
TEST(LinkReplay, CumulativeAcknowledgement)
{
    ReplayWindow window(4096, 128, 16);
    auto tlp = Tlp::request(TlpKind::MemoryRead, 0x1000, 64, 1, 2);
    for (unsigned cookie = 1; cookie <= 4; ++cookie) {
        tlp.cookie = cookie;
        ASSERT_TRUE(window.append(tlp));
    }
    auto ack = window.acknowledge(2);
    EXPECT_TRUE(ack.valid);
    EXPECT_TRUE(ack.progress);
    EXPECT_EQ(ack.cookies, (std::vector<uint64_t>{1, 2, 3}));
    EXPECT_EQ(window.size(), 1);
    auto duplicate = window.acknowledge(2);
    EXPECT_TRUE(duplicate.valid);
    EXPECT_FALSE(duplicate.progress);
    EXPECT_FALSE(window.acknowledge(0).valid);
    EXPECT_FALSE(window.acknowledge(4).valid);
    EXPECT_TRUE(window.acknowledge(3).valid);
    EXPECT_EQ(window.usedBytes(), 0);
}

/** 多次十二位序号回绕不影响副本与累计确认匹配。 */
TEST(LinkReplay, SequenceWrap)
{
    ReplayWindow window(4096, 128, 16);
    auto tlp = Tlp::request(TlpKind::MemoryRead, 0x1000, 64, 1, 2);
    for (unsigned count = 0; count < 10000; ++count) {
        auto entry = window.append(tlp);
        ASSERT_TRUE(entry);
        EXPECT_EQ(entry->sequence, count & 4095);
        EXPECT_TRUE(window.acknowledge(entry->sequence).valid);
    }
    EXPECT_EQ(window.usedBytes(), 0);
}

/** ACK 释放后，已开始发送的重传仍可持有不可变副本。 */
TEST(LinkReplay, InFlightCopyLifetime)
{
    ReplayWindow window(4096, 128, 16);
    auto tlp = Tlp::request(TlpKind::MemoryRead, 0x1000, 64, 1, 2);
    auto entry = window.append(tlp);
    auto copy = entry->packet;
    window.acknowledge(entry->sequence);
    EXPECT_EQ(entry->packet, copy);
    EXPECT_FALSE(window.find(entry->sequence));
}

/** 超大输入不能通过整数回绕绕过 replay 容量检查。 */
TEST(LinkReplay, OversizedPacket)
{
    ReplayWindow window(4096, 128, 16);
    EXPECT_FALSE(window.canAppend(UINT32_MAX));
    EXPECT_FALSE(window.canAppend(4096));
}
