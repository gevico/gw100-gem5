/**
 * @file  int_ctrl.test.cc
 * @brief IntCtrl 的单元测试
 *
 * 按 spec-pcie §8.4 检查 MSI-X 的待发、合并、Pending（粘滞、事件被处理
 * 时撤销）、Function Mask、Bus Master Enable、发送通路满、CTX_VECTOR
 * 与跨向量的来源、模式切换、FLR 门控、INT_LEVEL 的重新置位，以及
 * INTx 的电平。
 *
 * @see spec-pcie
 */

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "src/gw100/pcie/intr/int_ctrl.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

/** 假的发送通路：记录 MSI-X 消息 */
class TestHost : public HdmaHostIf
{
  public:
    /** 一条消息 */
    struct Msg
    {
        /** 地址 */
        Addr addr;
        /** 数据 */
        uint32_t data;
    };
    /** 发出的消息 */
    std::vector<Msg> msgs;
    /** 发送通路能否接受 */
    bool txOk = true;

    /** 不用 */
    void read(Addr, unsigned, HostReadDone) override {}
    /** 发送通路能否再接受一个写请求 */
    bool canWrite() const override { return txOk; }

    /** 记录一条消息 */
    void
    write(Addr a, unsigned len, const uint8_t *d, uint8_t = 0) override
    {
        uint32_t v = 0;
        std::memcpy(&v, d, len);
        msgs.push_back({a, v});
    }
};

/** INT_STATUS.ADMIN */
constexpr uint32_t Admin = 1u << 0;
/** INT_STATUS.ERROR */
constexpr uint32_t Error = 1u << 1;
/** INT_STATUS.FW_EVENT */
constexpr uint32_t FwEvent = 1u << 2;
/** INT_STATUS.CTX[n] */
constexpr uint32_t ctx(unsigned n) { return 1u << (16 + n); }

/** 测试台 */
struct Rig
{
    /** INT_STATUS */
    StatusUnit status{"test"};
    /** 发送通路 */
    TestHost host;
    /** 被测的中断控制器 */
    IntCtrl ic{status, host, 32, 16};
    /** INTA 的电平 */
    bool inta = false;

    /** 配置表项：地址 0x1000 + 16v，数据 0xA000 + v，不屏蔽 */
    Rig()
    {
        ic.onIntx = [this](bool l) { inta = l; };
        for (unsigned v = 0; v < 32; v++) {
            ic.tableWrite(v, 0, 0x1000 + 16 * v);
            ic.tableWrite(v, 1, 0);
            ic.tableWrite(v, 2, 0xA000 + v);
            ic.tableWrite(v, 3, 0);
        }
        ic.setBusMaster(true);
        ic.setMsix(true, false);
    }

    /** 最后一条消息的向量号 */
    unsigned last() const { return host.msgs.back().data - 0xA000; }
};

} // anonymous namespace

TEST(IntCtrl, TableResetValues)
{
    StatusUnit s("test");
    TestHost h;
    IntCtrl ic(s, h, 32, 16);
    EXPECT_EQ(ic.tableRead(5, 3), 1u);   // Mask 复位为 1
    EXPECT_EQ(ic.tableRead(5, 0), 0u);
    EXPECT_EQ(ic.ctxVector(0), 2u);
    EXPECT_EQ(ic.ctxVector(15), 17u);
}

TEST(IntCtrl, EventsSendToVectors)
{
    Rig r;
    r.status.setInt(Admin);
    ASSERT_EQ(r.host.msgs.size(), 1u);
    EXPECT_EQ(r.host.msgs[0].addr, 0x1000u);
    EXPECT_EQ(r.last(), 0u);
    r.status.logError(StatusUnit::ErrRsvdAccess, 0x40, false, 4, 0);
    EXPECT_EQ(r.last(), 1u);                 // ERROR → 向量 1
    r.ic.request(FwEvent | ctx(3));
    ASSERT_EQ(r.host.msgs.size(), 4u);
    // FW_EVENT → 向量 0，CTX[3] → 向量 5；轮询从上次发出的向量 1 之后
    // 开始，所以先发 5
    EXPECT_EQ(r.host.msgs[2].data, 0xA005u);
    EXPECT_EQ(r.host.msgs[3].data, 0xA000u);
    // 再次请求已置位的位，仍算一次事件
    r.ic.request(FwEvent);
    EXPECT_EQ(r.host.msgs.size(), 5u);
    EXPECT_FALSE(r.inta);                    // MSI-X 模式不用 INTA
}

TEST(IntCtrl, MaskSetsStickyPendingThenSendsOnUnmask)
{
    Rig r;
    r.ic.tableWrite(0, 3, 1);
    r.status.setInt(Admin);
    r.status.setInt(FwEvent);                // 同一向量合并
    EXPECT_TRUE(r.host.msgs.empty());
    EXPECT_EQ(r.ic.pba(), 1u);
    r.host.txOk = false;
    r.ic.tableWrite(0, 3, 0);                // 解除屏蔽但通路满
    EXPECT_EQ(r.ic.pba(), 1u);               // Pending 保持
    r.host.txOk = true;
    r.ic.trySend();
    ASSERT_EQ(r.host.msgs.size(), 1u);       // 只发一条
    EXPECT_EQ(r.ic.pba(), 0u);
}

TEST(IntCtrl, ServicedWhileMaskedClearsPending)
{
    Rig r;
    r.ic.tableWrite(0, 3, 1);
    r.status.setInt(Admin | FwEvent);
    EXPECT_EQ(r.ic.pba(), 1u);
    r.status.clearInt(Admin);
    EXPECT_EQ(r.ic.pba(), 1u);               // FW_EVENT 仍在
    r.status.clearInt(FwEvent);
    EXPECT_EQ(r.ic.pba(), 0u);               // 全部处理
    r.ic.tableWrite(0, 3, 0);
    EXPECT_TRUE(r.host.msgs.empty());        // 不再发出
}

TEST(IntCtrl, FunctionMaskAndBusMaster)
{
    Rig r;
    r.ic.setMsix(true, true);
    r.status.setInt(Admin);
    EXPECT_EQ(r.ic.pba(), 1u);
    r.ic.setMsix(true, false);
    EXPECT_EQ(r.host.msgs.size(), 1u);
    r.ic.setBusMaster(false);
    r.status.setInt(Admin);
    EXPECT_EQ(r.ic.pba(), 1u);
    EXPECT_EQ(r.host.msgs.size(), 1u);
    r.ic.setBusMaster(true);
    EXPECT_EQ(r.host.msgs.size(), 2u);
    EXPECT_EQ(r.ic.pba(), 0u);
}

TEST(IntCtrl, TxFullDoesNotSetPending)
{
    Rig r;
    r.host.txOk = false;
    r.status.setInt(Admin);
    EXPECT_EQ(r.ic.pba(), 0u);
    EXPECT_TRUE(r.host.msgs.empty());
    r.host.txOk = true;
    r.ic.trySend();
    EXPECT_EQ(r.host.msgs.size(), 1u);
}

TEST(IntCtrl, CtxVectorAndCrossVectorSources)
{
    Rig r;
    r.ic.tableWrite(2, 3, 1);
    r.ic.tableWrite(5, 3, 1);
    r.ic.setCtxVector(3, 2);
    r.ic.request(ctx(3));
    r.ic.setCtxVector(3, 5);
    r.ic.request(ctx(3));
    EXPECT_EQ(r.ic.pba(), (1u << 2) | (1u << 5));
    r.status.clearInt(ctx(3));               // 从两个来源集合都删去
    EXPECT_EQ(r.ic.pba(), 0u);
}

TEST(IntCtrl, LevelReassertIsAnEvent)
{
    Rig r;
    r.ic.setLevel(Admin);
    EXPECT_TRUE(r.host.msgs.empty());        // 写 INT_LEVEL 不置位
    r.ic.request(Admin);
    EXPECT_EQ(r.host.msgs.size(), 1u);
    r.status.clearInt(Admin);                // 源仍成立：重新置位
    EXPECT_TRUE(r.status.intStatus() & Admin);
    EXPECT_EQ(r.host.msgs.size(), 2u);
    r.ic.setLevel(0);
    r.status.clearInt(Admin);
    EXPECT_FALSE(r.status.intStatus() & Admin);
    EXPECT_EQ(r.host.msgs.size(), 2u);
}

TEST(IntCtrl, ModeSwitchClearsAndIntx)
{
    Rig r;
    r.ic.tableWrite(0, 3, 1);
    r.status.setInt(Admin);
    EXPECT_EQ(r.ic.pba(), 1u);
    r.ic.setMsix(false, false);              // 切回 INTx
    EXPECT_EQ(r.ic.pba(), 0u);
    EXPECT_FALSE(r.inta);                    // INT_MASK 复位为全 1
    r.status.setIntMask(~Admin);
    EXPECT_TRUE(r.inta);
    r.ic.setIntxDisable(true);
    EXPECT_FALSE(r.inta);
    r.ic.setIntxDisable(false);
    EXPECT_TRUE(r.inta);
    r.status.clearInt(Admin);
    EXPECT_FALSE(r.inta);
    r.status.setInt(Admin);
    EXPECT_TRUE(r.inta);
    r.ic.setMsix(true, false);               // 切到 MSI-X：撤销 INTA，不补发
    EXPECT_FALSE(r.inta);
    r.ic.tableWrite(0, 3, 0);
    EXPECT_TRUE(r.host.msgs.empty());
}

TEST(IntCtrl, FlrGate)
{
    Rig r;
    r.ic.setFlrGate(true);
    r.ic.request(Admin);                     // 被忽略
    EXPECT_FALSE(r.status.intStatus() & Admin);
    r.ic.setCtxVector(0, 9);
    EXPECT_EQ(r.ic.ctxVector(0), 2u);
    r.status.setInt(FwEvent);                // host 的 INT_SET 照常
    EXPECT_TRUE(r.host.msgs.empty());
    r.ic.setFlrGate(false);
    EXPECT_EQ(r.host.msgs.size(), 1u);
}

TEST(IntCtrl, ResetRestoresTable)
{
    Rig r;
    r.ic.setCtxVector(1, 7);
    r.ic.reset();
    EXPECT_EQ(r.ic.tableRead(0, 3), 1u);
    EXPECT_EQ(r.ic.tableRead(0, 2), 0u);
    EXPECT_EQ(r.ic.ctxVector(1), 3u);
}
