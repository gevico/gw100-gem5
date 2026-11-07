/**
 * @file  memory_control.hh
 * @brief Memory Tile 的软件寄存器、保护与初始化状态
 *
 * 纯数字逻辑由外壳提供媒体命令和训练完成事件，便于独立验证。
 *
 * @see spec-dram, spec-address-map
 */

#ifndef GW100_MEMORY_CONTROL_HH
#define GW100_MEMORY_CONTROL_HH

#include <array>
#include <cstdint>
#include <functional>

#include "src/gw100/mem_tile/generated/memory_defs.hh"

namespace gem5::gw100
{

/** 一个 Memory Tile 的控制与保护逻辑 */
class MemoryControl
{
  public:
    /** 一个控制器通道 */
    struct Channel
    {
        /** 固件选择的档案 */
        uint32_t profile = 0;
        /** 初始化和生命周期状态 */
        uint32_t state = memorydefs::STATE_RESET;
        /** 当前校准或训练阶段中两个 rank 的成功位 */
        uint32_t trained = 0;
        /** 命令错误，rank 位于 [15:8] */
        uint32_t error = 0;
        /** 完成命令数 */
        uint64_t sequence = 0;
        /** 排空前的 TEST 或 READY 状态 */
        uint32_t runState = memorydefs::STATE_TEST;
    };

    /** 一项全局 DPA 保护区域 */
    struct Region
    {
        /** 区间起始，含 */
        uint64_t base = 0;
        /** 区间终止，不含 */
        uint64_t limit = 0;
        /** 启用位 */
        bool enabled = false;
    };

    /** 构造指定编号的 Tile */
    explicit MemoryControl(unsigned tile);
    /** 清除配置和训练结果，递增复位代次 */
    void reset();
    /** 读取自然对齐的控制寄存器，失败返回 false */
    bool read(uint64_t off, unsigned size, uint64_t &value) const;
    /** 写入控制寄存器，返回错误编码 */
    unsigned write(uint64_t off, unsigned size, uint64_t value);
    /** 校验一个完整数据请求的地址和保护权限 */
    bool permitted(uint64_t addr, unsigned size, unsigned source) const;
    /** 发起一个通道命令，返回错误编码 */
    unsigned command(unsigned channel, unsigned cmd);
    /** 完成通道命令，保存状态、结果和完成次数 */
    void complete(unsigned channel, unsigned state, unsigned error = 0);
    /** 记录首个未清除错误及位图 */
    void log(unsigned error, unsigned source, uint64_t addr, uint64_t txn,
             uint64_t info = 0);
    /** 通道处于异步命令期间 */
    bool busy(unsigned channel) const;

    /** Tile 编号 */
    const unsigned tile;
    /** 互连与 Tile 已释放 */
    bool released = false;
    /** DDR PLL 已锁定 */
    bool pllLocked = false;
    /** 内存复位代次 */
    uint64_t epoch = 0;
    /** 保护表已锁定 */
    bool locked = false;
    /** 基础计数器快照已冻结 */
    bool frozen = false;
    /** 两个通道 */
    std::array<Channel, 2> channels;
    /** 四项保护区域 */
    std::array<Region, 4> regions;
    /** 请求、读字节、写字节、retry、CDC、排队、传输计数 */
    std::array<uint64_t, 7> counters{};
    /** 冻结快照 */
    std::array<uint64_t, 7> snapshot{};
    /** 错误位图 */
    uint32_t errors = 0;
    /** 首个错误的来源 */
    uint32_t errorSource = 0;
    /** 首个错误的 DPA */
    uint64_t errorAddr = 0;
    /** 首个错误的事务号 */
    uint64_t errorTxn = 0;
    /** 首个错误的通道、rank 与阶段 */
    uint64_t errorInfo = 0;
    /** 外壳执行一个已接受的异步命令 */
    std::function<void(unsigned, unsigned)> onCommand;
};

} // namespace gem5::gw100
#endif
