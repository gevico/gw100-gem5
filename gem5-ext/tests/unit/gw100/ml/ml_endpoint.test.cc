/**
 * @file  ml_endpoint.test.cc
 * @brief MlEndpoint 与 AsyncFifo 的单元测试
 *
 * 用假时钟驱动 ML 的两端：HIF 侧 250 MHz，管理 Tile 侧 500 MHz（或
 * 25 MHz），按 spec-mgmt-link 的验收标准检查建立、顺序、tag、屏障、
 * 撤销、超时与故障、非法请求、单程延迟和吞吐。
 *
 * @see spec-mgmt-link
 */

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "src/gw100/ml/ml_endpoint.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

/** 1 ns 对应的 tick 数（gem5 默认 1 ps 一个 tick） */
constexpr Tick ns = 1000;

class Sim;

/** 测试用的时钟：固定周期，唤醒请求登记到 Sim */
class TestClock : public MlClockIf
{
  public:
    /** 构造：period 为周期，id 为所属端点在 Sim 中的编号 */
    TestClock(Sim &sim, Tick period, int id)
        : sim(sim), per(period), id(id) {}

    /** 当前时刻 */
    Tick now() const override;

    /** 时刻 t 之后（不含 t）的第一个时钟沿 */
    Tick
    edgeAfter(Tick t) const override
    {
        return (t / per + 1) * per;
    }

    /** 当前周期 */
    Tick period() const override { return per; }

    /** 请求在 when 时刻或之后的第一个时钟沿调用处理函数 */
    void wakeAt(Tick when) override;

    /** 所属的仿真 */
    Sim &sim;
    /** 周期 */
    Tick per;
    /** 端点编号 */
    int id;
};

/** 两端加四个通道组成的测试台，自带一个简单的事件循环 */
class Sim
{
  public:
    /** 构造：两端的周期和配置 */
    Sim(Tick hif_period, Tick mgmt_period, MlEndpoint::Config hif_cfg,
        MlEndpoint::Config mgmt_cfg)
        : hifClk(*this, hif_period, 0), mgmtClk(*this, mgmt_period, 1),
          h2mReq(8, 2), m2hRsp(8, 2), m2hReq(8, 2), h2mRsp(8, 2),
          hif("hif", hif_cfg), mgmt("mgmt", mgmt_cfg)
    {
        h2mReq.bind(&hifClk, &mgmtClk);
        h2mRsp.bind(&hifClk, &mgmtClk);
        m2hReq.bind(&mgmtClk, &hifClk);
        m2hRsp.bind(&mgmtClk, &hifClk);
    }

    /** 绑定两端的目标并让两端撤销复位 */
    void
    start(MlTargetIf *hif_tgt, MlTargetIf *mgmt_tgt)
    {
        hif.bind(&hifClk, hif_tgt, &h2mReq, &h2mRsp, &m2hReq, &m2hRsp,
                 &mgmt);
        mgmt.bind(&mgmtClk, mgmt_tgt, &m2hReq, &m2hRsp, &h2mReq, &h2mRsp,
                  &hif);
        hif.resetRelease();
        mgmt.resetRelease();
    }

    /** 运行到 until 时刻，或者没有待处理的唤醒为止 */
    void
    run(Tick until)
    {
        while (!wakes.empty()) {
            auto it = wakes.begin();
            if (it->first > until)
                break;
            Tick t = it->first;
            int id = it->second;
            wakes.erase(it);
            cur = t;
            (id == 0 ? hif : mgmt).process();
        }
        cur = std::max(cur, until);
    }

    /** 登记一个唤醒 */
    void
    wake(Tick t, int id)
    {
        wakes.insert({t, id});
    }

    /** 当前时刻 */
    Tick cur = 0;
    /** HIF 侧时钟 */
    TestClock hifClk;
    /** 管理 Tile 侧时钟 */
    TestClock mgmtClk;
    /** HIF → 管理 Tile 请求通道 */
    MlEndpoint::ReqFifo h2mReq;
    /** 管理 Tile → HIF 响应通道 */
    MlEndpoint::RspFifo m2hRsp;
    /** 管理 Tile → HIF 请求通道 */
    MlEndpoint::ReqFifo m2hReq;
    /** HIF → 管理 Tile 响应通道 */
    MlEndpoint::RspFifo h2mRsp;
    /** HIF 侧适配器 */
    MlEndpoint hif;
    /** 管理 Tile 侧适配器 */
    MlEndpoint mgmt;
    /** 待处理的唤醒：（时刻，端点编号） */
    std::set<std::pair<Tick, int>> wakes;
};

/** 当前时刻 */
Tick
TestClock::now() const
{
    return sim.cur;
}

/** 请求在 when 时刻或之后的第一个时钟沿调用处理函数 */
void
TestClock::wakeAt(Tick when)
{
    Tick t = std::max(when, sim.cur);
    if (t % per)
        t = edgeAfter(t);
    sim.wake(t, id);
}

/** 记录收到的请求的目标；可设置为不响应或拒绝 */
class RecTarget : public MlTargetIf
{
  public:
    /** 处理对端发来的一个请求 */
    void
    mlRequest(const MlTxnPtr &txn, MlRespond respond) override
    {
        got.push_back(txn);
        gotAt.push_back(sim ? sim->cur : 0);
        if (silent)
            return;
        MlData d{};
        for (unsigned i = 0; i < mlMaxPayload; i++)
            d[i] = static_cast<uint8_t>(txn->addr + i);
        respond(reject ? MlStatus::Reject : MlStatus::Ok, d);
    }

    /** 收到的请求，按交给目标的顺序 */
    std::vector<MlTxnPtr> got;
    /** 各请求交给目标的时刻 */
    std::vector<Tick> gotAt;
    /** 为 true 时不响应（用于超时） */
    bool silent = false;
    /** 为 true 时以 REJECT 响应 */
    bool reject = false;
    /** 用于记录时刻的测试台 */
    Sim *sim = nullptr;
};

/** 默认的 HIF 侧配置：32 个 tag，超时 100 µs */
MlEndpoint::Config
hifCfg()
{
    MlEndpoint::Config c;
    c.tags = 32;
    c.timeout = 100000 * ns;
    return c;
}

/** 默认的管理 Tile 侧配置：4 个 tag，超时 100 µs */
MlEndpoint::Config
mgmtCfg()
{
    MlEndpoint::Config c;
    c.tags = 4;
    c.timeout = 100000 * ns;
    c.mgmtSide = true;
    return c;
}

/** 构造一个事务 */
MlTxnPtr
txn(MlType type, Addr addr, unsigned len, MlDone done = nullptr)
{
    auto t = std::make_shared<MlTxn>();
    t->type = type;
    t->addr = addr;
    t->len = len;
    t->be = len == 64 ? ~0ULL : ((1ULL << len) - 1);
    t->done = done;
    return t;
}

} // anonymous namespace

/** F5：两端撤销复位后，都在看到对端就绪后进入 UP */
TEST(MlEndpointTest, LinkComesUp)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    sim.start(&ht, &mt);
    EXPECT_EQ(sim.hif.state(), MlLinkState::Down);
    sim.run(100 * ns);
    EXPECT_EQ(sim.hif.state(), MlLinkState::Up);
    EXPECT_EQ(sim.mgmt.state(), MlLinkState::Up);
    EXPECT_EQ(sim.mgmt.statusReg(), 1u);
}

/** DOWN 时适配器不接受请求 */
TEST(MlEndpointTest, DownRejectsSubmission)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    sim.start(&ht, &mt);
    unsigned c = sim.hif.addClient();
    EXPECT_FALSE(sim.hif.submit(c, txn(MlType::Event, 0, 4)));
}

/** F1、F2：posted 写按发出顺序到达，数据正确 */
TEST(MlEndpointTest, PostedWritesInOrder)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    sim.start(&ht, &mt);
    unsigned c = sim.hif.addClient();
    sim.run(100 * ns);
    for (unsigned i = 0; i < 6; i++) {
        auto t = txn(i % 2 ? MlType::Event : MlType::DbWr, i, 4);
        t->data[0] = 0xA0 + i;
        ASSERT_TRUE(sim.hif.submit(c, t));
    }
    sim.run(2000 * ns);
    ASSERT_EQ(mt.got.size(), 6u);
    for (unsigned i = 0; i < 6; i++) {
        EXPECT_EQ(mt.got[i]->addr, i);
        EXPECT_EQ(mt.got[i]->data[0], 0xA0 + i);
    }
}

/** 读请求得到响应数据；BOOT_WR 得到 OK */
TEST(MlEndpointTest, ReadAndNonPostedWrite)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    sim.start(&ht, &mt);
    unsigned c = sim.mgmt.addClient();
    unsigned h = sim.hif.addClient();
    sim.run(100 * ns);
    MlStatus rd_st = MlStatus::Fault;
    MlData rd_data{};
    ASSERT_TRUE(sim.mgmt.submit(c, txn(MlType::RegRd, 0x100, 4,
        [&](MlStatus s, const MlData &d) { rd_st = s; rd_data = d; })));
    MlStatus wr_st = MlStatus::Fault;
    ASSERT_TRUE(sim.hif.submit(h, txn(MlType::BootWr, 0x40, 64,
        [&](MlStatus s, const MlData &) { wr_st = s; })));
    sim.run(2000 * ns);
    EXPECT_EQ(rd_st, MlStatus::Ok);
    EXPECT_EQ(rd_data[0], 0x00);
    EXPECT_EQ(rd_data[1], 0x01);
    EXPECT_EQ(wr_st, MlStatus::Ok);
    EXPECT_EQ(sim.mgmt.outstanding(), 0u);
    EXPECT_EQ(sim.hif.outstanding(), 0u);
}

/** F4：在途 non-posted 请求不超过 tag 数 */
TEST(MlEndpointTest, TagLimit)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    ht.silent = true;
    sim.start(&ht, &mt);
    unsigned c = sim.mgmt.addClient();
    sim.run(100 * ns);
    for (unsigned i = 0; i < 6; i++)
        ASSERT_TRUE(sim.mgmt.submit(c, txn(MlType::RegRd, i * 4, 4)));
    sim.run(5000 * ns);
    EXPECT_EQ(sim.mgmt.outstanding(), 4u);  // 不超过 ml.mgmt_tags
    EXPECT_EQ(ht.got.size(), 1u);           // 目标一次只处理一个
}

/** tag 用完时，其他客户端的 posted 写仍能发出 */
TEST(MlEndpointTest, PostedNotBlockedByTags)
{
    MlEndpoint::Config hc = hifCfg();
    hc.tags = 1;
    Sim sim(4 * ns, 2 * ns, hc, mgmtCfg());
    RecTarget ht, mt;
    mt.silent = true;  // 第一个读不返回，tag 一直被占用
    sim.start(&ht, &mt);
    unsigned a = sim.hif.addClient();
    unsigned b = sim.hif.addClient();
    sim.run(100 * ns);
    ASSERT_TRUE(sim.hif.submit(a, txn(MlType::DescRd, 0, 32)));
    ASSERT_TRUE(sim.hif.submit(a, txn(MlType::DescRd, 32, 32)));
    ASSERT_TRUE(sim.hif.submit(b, txn(MlType::DbWr, 3, 4)));
    sim.run(2000 * ns);
    EXPECT_EQ(sim.hif.outstanding(), 1u);
    // 目标正在处理第一个读，posted 写排在它之后的通道中，尚未交出；
    // 但它已经发出（没有被 tag 挡住）
    EXPECT_EQ(sim.h2mReq.occupancy(sim.cur), 1u);
}

/** F7：屏障事件在此前被接受的全部请求发出之后才发出 */
TEST(MlEndpointTest, BarrierOrdersAcrossClients)
{
    MlEndpoint::Config hc = hifCfg();
    hc.tags = 2;
    Sim sim(4 * ns, 2 * ns, hc, mgmtCfg());
    RecTarget ht, mt;
    sim.start(&ht, &mt);
    unsigned a = sim.hif.addClient();
    unsigned b = sim.hif.addClient();
    sim.run(100 * ns);
    ASSERT_TRUE(sim.hif.submit(a, txn(MlType::DescRd, 0, 32)));
    ASSERT_TRUE(sim.hif.submit(a, txn(MlType::DescWr, 64, 4)));
    ASSERT_TRUE(sim.hif.submit(a, txn(MlType::DescRd, 128, 32)));
    MlStatus bs = MlStatus::Fault;
    ASSERT_TRUE(sim.hif.submit(b, txn(MlType::Barrier, 0, 4,
        [&](MlStatus s, const MlData &) { bs = s; })));
    ASSERT_TRUE(sim.hif.submit(a, txn(MlType::DbWr, 7, 4)));
    sim.run(5000 * ns);
    ASSERT_EQ(mt.got.size(), 5u);
    EXPECT_EQ(mt.got[0]->addr, 0u);
    EXPECT_EQ(mt.got[1]->addr, 64u);
    EXPECT_EQ(mt.got[2]->addr, 128u);
    EXPECT_EQ(mt.got[3]->type, MlType::Barrier);
    EXPECT_EQ(mt.got[4]->type, MlType::DbWr);
    EXPECT_EQ(bs, MlStatus::Ok);
}

/** F8：超时使两端都进入 FAILED，在途请求以 FAULT 结束 */
TEST(MlEndpointTest, TimeoutFailsBothSides)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    ht.silent = true;
    sim.start(&ht, &mt);
    unsigned c = sim.mgmt.addClient();
    sim.run(100 * ns);
    MlStatus st = MlStatus::Ok;
    ASSERT_TRUE(sim.mgmt.submit(c, txn(MlType::RegRd, 0x10, 4,
        [&](MlStatus s, const MlData &) { st = s; })));
    sim.run(99000 * ns);
    EXPECT_EQ(sim.mgmt.state(), MlLinkState::Up);
    sim.run(101000 * ns);
    EXPECT_EQ(st, MlStatus::Fault);
    EXPECT_EQ(sim.mgmt.state(), MlLinkState::Failed);
    EXPECT_TRUE(sim.mgmt.errReg() & (1u << ErrTimeout));
    EXPECT_EQ((sim.mgmt.errInfoReg() >> 9) & 0x1F, ErrTimeout);
    EXPECT_EQ(sim.mgmt.errAddrReg(), 0x10u);
    sim.run(102000 * ns);
    EXPECT_EQ(sim.hif.state(), MlLinkState::Failed);
    EXPECT_TRUE(sim.hif.errReg() & (1u << ErrPeerFailed));
    // 故障态下的新请求立即以 FAULT 结束
    MlStatus st2 = MlStatus::Ok;
    ASSERT_TRUE(sim.mgmt.submit(c, txn(MlType::RegRd, 0x20, 4,
        [&](MlStatus s, const MlData &) { st2 = s; })));
    EXPECT_EQ(st2, MlStatus::Fault);
}

/** F7：撤销的请求的响应被丢弃，tag 在响应到达后释放 */
TEST(MlEndpointTest, WithdrawDropsResponse)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    sim.start(&ht, &mt);
    unsigned c = sim.hif.addClient();
    sim.run(100 * ns);
    bool called = false;
    ASSERT_TRUE(sim.hif.submit(c, txn(MlType::DescRd, 0, 32,
        [&](MlStatus, const MlData &) { called = true; })));
    sim.hif.withdraw(c);
    sim.run(2000 * ns);
    EXPECT_FALSE(called);
    EXPECT_EQ(mt.got.size(), 1u);  // 已接受的请求照常发出
    EXPECT_EQ(sim.hif.outstanding(), 0u);
}

/** F9：读收到 REJECT；posted 写被拒绝时由接收方记录 */
TEST(MlEndpointTest, RejectHandling)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    ht.reject = true;
    mt.reject = true;
    sim.start(&ht, &mt);
    unsigned c = sim.mgmt.addClient();
    unsigned h = sim.hif.addClient();
    sim.run(100 * ns);
    MlStatus st = MlStatus::Ok;
    ASSERT_TRUE(sim.mgmt.submit(c, txn(MlType::RegRd, 0, 4,
        [&](MlStatus s, const MlData &) { st = s; })));
    ASSERT_TRUE(sim.hif.submit(h, txn(MlType::DbWr, 99, 4)));
    sim.run(2000 * ns);
    EXPECT_EQ(st, MlStatus::Reject);
    EXPECT_TRUE(sim.mgmt.errReg() & (1u << ErrNpReject));
    EXPECT_TRUE(sim.mgmt.errReg() & (1u << ErrWrReject));
    EXPECT_EQ(sim.mgmt.state(), MlLinkState::Up);
    sim.mgmt.clearErr(0xFF);
    EXPECT_EQ(sim.mgmt.errReg(), 0u);
}

/** F1：客户端提交不合规则的请求，不发出，记录 BAD_REQ */
TEST(MlEndpointTest, BadRequest)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    sim.start(&ht, &mt);
    unsigned h = sim.hif.addClient();
    sim.run(100 * ns);
    MlStatus st = MlStatus::Ok;
    // BOOT_WR 长度不是 8 的倍数
    ASSERT_TRUE(sim.hif.submit(h, txn(MlType::BootWr, 0, 12,
        [&](MlStatus s, const MlData &) { st = s; })));
    EXPECT_EQ(st, MlStatus::Reject);
    EXPECT_TRUE(sim.hif.errReg() & (1u << ErrBadReq));
    sim.run(1000 * ns);
    EXPECT_TRUE(mt.got.empty());
}

/** P1：单程延迟不超过 40 ns（管理 Tile 侧 500 MHz） */
TEST(MlEndpointTest, OneWayLatency)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    mt.sim = &sim;
    sim.start(&ht, &mt);
    unsigned h = sim.hif.addClient();
    sim.run(100 * ns);
    Tick t0 = sim.cur;
    ASSERT_TRUE(sim.hif.submit(h, txn(MlType::Event, 0, 4)));
    sim.run(1000 * ns);
    ASSERT_EQ(mt.got.size(), 1u);
    EXPECT_LE(mt.gotAt[0] - t0, 40 * ns);
}

/** P5：无竞争时请求通道按较慢一端每周期一拍传输 */
TEST(MlEndpointTest, Throughput)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    mt.sim = &sim;
    sim.start(&ht, &mt);
    unsigned h = sim.hif.addClient();
    sim.run(100 * ns);
    const unsigned n = 32;  // 32 个 DB_WR，每个 1 拍
    for (unsigned i = 0; i < n; i++)
        ASSERT_TRUE(sim.hif.submit(h, txn(MlType::DbWr, i, 4)));
    Tick t0 = sim.cur;
    sim.run(10000 * ns);
    ASSERT_EQ(mt.got.size(), n);
    Tick span = mt.gotAt[n - 1] - mt.gotAt[0];
    // 每拍 4 ns（HIF 侧较慢），允许少量启动开销
    EXPECT_LE(span, (n - 1) * 4 * ns + 4 * ns);
    (void)t0;
}

/** 对端复位：在途请求以 FAULT 结束并记录 PEER_RESET，链路回到 DOWN */
TEST(MlEndpointTest, PeerReset)
{
    Sim sim(4 * ns, 2 * ns, hifCfg(), mgmtCfg());
    RecTarget ht, mt;
    ht.silent = true;
    sim.start(&ht, &mt);
    unsigned c = sim.mgmt.addClient();
    sim.run(100 * ns);
    MlStatus st = MlStatus::Ok;
    ASSERT_TRUE(sim.mgmt.submit(c, txn(MlType::RegRd, 0, 4,
        [&](MlStatus s, const MlData &) { st = s; })));
    sim.run(200 * ns);
    sim.hif.resetAssert();
    sim.run(300 * ns);
    EXPECT_EQ(st, MlStatus::Fault);
    EXPECT_EQ(sim.mgmt.state(), MlLinkState::Down);
    EXPECT_TRUE(sim.mgmt.errReg() & (1u << ErrPeerReset));
    sim.mgmt.resetAssert();
    sim.hif.resetRelease();
    sim.mgmt.resetRelease();
    sim.run(500 * ns);
    EXPECT_EQ(sim.mgmt.state(), MlLinkState::Up);
    EXPECT_EQ(sim.hif.state(), MlLinkState::Up);
}
