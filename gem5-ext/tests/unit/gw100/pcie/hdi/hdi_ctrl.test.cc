/**
 * @file  hdi_ctrl.test.cc
 * @brief HdiCtrl 的单元测试
 *
 * 检查复位值、可写字段与 EN = 1 时的写保护、HDI_WR_MAP 与 HDI_WR 事件、
 * BUSY 的握手（spec-hdi §5.5）、FLR 门控、doorbell 待发槽的覆盖与按
 * 最后一次写入的先后发出（spec-pcie §5.3）。
 *
 * @see spec-hdi, spec-pcie
 */

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "src/gw100/pcie/hdi/hdi_ctrl.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

/** 测试台：记录发出的 DB_WR 和 HDI_WR 事件 */
struct Rig
{
    /** 被测对象 */
    HdiCtrl h;
    /** 发出的 DB_WR：槽号与值 */
    std::vector<std::pair<unsigned, uint16_t>> sent;
    /** 链路是否建立 */
    bool up = true;
    /** HDI_WR 事件次数 */
    unsigned events = 0;

    /** 接上回调 */
    Rig()
    {
        h.sendDb = [this](unsigned s, uint16_t v) {
            if (!up)
                return false;
            sent.push_back({s, v});
            return true;
        };
        h.onHdiWr = [this] { events++; };
    }

    /** 固件读 */
    uint32_t
    fw(Addr off)
    {
        uint32_t v = 0;
        EXPECT_TRUE(h.fwRead(off, v));
        return v;
    }
};

} // anonymous namespace

TEST(HdiCtrl, ResetValues)
{
    Rig r;
    EXPECT_EQ(r.h.hostRead(0x000), 4095u | (16u << 16) | (4u << 24));
    EXPECT_EQ(r.h.hostRead(0x004), 64u | (8u << 8));
    EXPECT_EQ(r.h.hostRead(0x008), 0x100u);
    EXPECT_EQ(r.h.hostRead(0x014), 0u);
    EXPECT_TRUE(HdiCtrl::defined(0x034));
    EXPECT_FALSE(HdiCtrl::defined(0x00C));
    EXPECT_FALSE(HdiCtrl::defined(0x038));
}

TEST(HdiCtrl, WritableFieldsAndMap)
{
    Rig r;
    r.h.hostWrite(0x018, 0xFFFFFFFF);                // AQA
    EXPECT_EQ(r.h.hostRead(0x018), 0x0FFF0FFFu);
    r.h.hostWrite(0x020, 0x12345678);                // ASQ_LO：4 KiB 对齐
    EXPECT_EQ(r.h.hostRead(0x020), 0x12345000u);
    r.h.hostWrite(0x000, 0);                         // 只读：忽略
    EXPECT_NE(r.h.hostRead(0x000), 0u);
    EXPECT_EQ(r.fw(HdiCtrl::WrMap), (1u << 6) | (1u << 8));
    EXPECT_EQ(r.events, 2u);
    r.h.hostWrite(HdiCtrl::Cc, 0xFFFFFFFF);          // EN、SHN
    EXPECT_EQ(r.h.hostRead(HdiCtrl::Cc), 0xC001u);
    r.h.hostWrite(0x018, 0);                         // EN = 1：AQA 被忽略
    EXPECT_EQ(r.h.hostRead(0x018), 0x0FFF0FFFu);
}

TEST(HdiCtrl, BusyHandshake)
{
    Rig r;
    r.h.hostWrite(HdiCtrl::Cc, 1);
    EXPECT_TRUE(r.h.hostRead(HdiCtrl::Csts) & HdiCtrl::CstsBusy);
    // 固件：先置自己的 BUSY，再清 map
    r.h.fwWrite(HdiCtrl::Csts, HdiCtrl::CstsBusy);
    r.h.fwWrite(HdiCtrl::WrMap, 1u << 4);
    EXPECT_TRUE(r.h.hostRead(HdiCtrl::Csts) & HdiCtrl::CstsBusy);
    // 处理期间 host 又写了 CC：清固件的位后 BUSY 仍为 1
    r.h.hostWrite(HdiCtrl::Cc, 0);
    r.h.fwWrite(HdiCtrl::Csts, 1);                   // RDY = 1，BUSY = 0
    EXPECT_TRUE(r.h.hostRead(HdiCtrl::Csts) & HdiCtrl::CstsBusy);
    EXPECT_EQ(r.fw(HdiCtrl::WrMap) & (1u << 4), 1u << 4);
    r.h.fwWrite(HdiCtrl::WrMap, 1u << 4);
    EXPECT_EQ(r.h.hostRead(HdiCtrl::Csts), 1u);      // BUSY = 0
}

TEST(HdiCtrl, FlrGateDropsWrites)
{
    Rig r;
    r.h.reset();
    r.h.setGate(true);
    r.h.hostWrite(HdiCtrl::Cc, 1);
    r.h.hostDoorbell(0, 0, 5);
    r.h.fwWrite(HdiCtrl::Csts, 1);
    EXPECT_EQ(r.h.hostRead(HdiCtrl::Cc), 0u);
    EXPECT_EQ(r.h.hostRead(HdiCtrl::Csts), 0u);
    EXPECT_TRUE(r.sent.empty());
    EXPECT_EQ(r.events, 0u);
}

TEST(HdiCtrl, DoorbellOrderAndCoalesce)
{
    Rig r;
    r.up = false;
    r.h.hostDoorbell(1, 0, 10);                      // 槽 2
    r.h.hostDoorbell(0, 0, 1);                       // 槽 0
    r.h.hostDoorbell(1, 0, 11);                      // 覆盖槽 2，移到最后
    r.h.hostDoorbell(0, 4, 3);                       // 槽 1
    EXPECT_TRUE(r.sent.empty());
    r.up = true;
    r.h.trySendDoorbells();
    ASSERT_EQ(r.sent.size(), 3u);
    EXPECT_EQ(r.sent[0], std::make_pair(0u, uint16_t(1)));
    EXPECT_EQ(r.sent[1], std::make_pair(2u, uint16_t(11)));
    EXPECT_EQ(r.sent[2], std::make_pair(1u, uint16_t(3)));
    r.h.hostDoorbell(16, 4, 7);                      // 最后一页的 CQ_HEAD
    EXPECT_EQ(r.sent.back(), std::make_pair(33u, uint16_t(7)));
    r.h.hostDoorbell(0, 8, 9);                       // 保留偏移：忽略
    EXPECT_EQ(r.sent.size(), 4u);
}
