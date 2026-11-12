/**
 * @file ct_types.hh
 * @brief GW100 Compute Tile 各组件的通用接口与类型
 * 
 * 
 * @see spec-compute-tile
 */

#ifndef __GW100_CT_CT_TYPES_HH__
#define __GW100_CT_CT_TYPES_HH__

#include <array>
#include <cstdint>

#include "base/types.hh"
#include "sim/serialize.hh"

namespace gem5
{
namespace gw100
{

/** Compute Tile 的基址 */
constexpr Addr CtBase = 0x10'0000'0000;
/** 单个 Compute Tile 窗口总大小 (16 MiB) */
constexpr Addr CtWindowSize = 0x0100'0000;
//todo: 后续具体的地址范围还需要进一步商讨，同时配置区的设计和本地引擎发射口的设计还需要考虑
/** 数据 SRAM (CtSram) 起始偏移与大小 (2 MiB) */
constexpr Addr CtSramBase = 0x0000'0000;
constexpr Addr CtSramSize = 0x0020'0000;
/** 控制核专属 RAM (CtrlRam) 起始偏移与大小 (32 KiB) */
constexpr Addr CtrlRamBase = 0x0040'0000;
constexpr Addr CtrlRamSize = 0x0000'8000;
/** 硬件同步器 (SyncUnit) 窗口偏移与大小 (32 个 32 位计数器 = 128 B) */
constexpr Addr CtSyncBase = 0x0080'0000;
constexpr Addr CtSyncSize = 0x0000'0080;

/** noc 传输包大小上限 */
constexpr Addr NocPacketLimit = 0x0000'0040;
/** 访问 bank 边界 */
constexpr Addr CtSramBankLimit = 0x0000'0100;


/** 返回状态码 */
enum class CtStatus : uint8_t
{
    Ok = 0,
    Reject,
    Fault, /** 被拒绝（访问越界等等） */
};

constexpr Addr
VisitSingleCt(int x, int y)
{
    return CtBase + (y * 8 + x) * CtWindowSize;
}

/** 地址访问的发起方 */
enum class CtInitiator : uint8_t
{
    LocalCpu = 0,
    LocalEngine,
    RemoteCpu,
    MT,
};

/** 地址访问的接口实现 */
struct CtAccess
{
    Addr base;
    uint8_t *data;
    bool write;
    CtInitiator initiator;
    unsigned size;
};

struct CtAccessResult
{
    CtStatus status = CtStatus::Ok;
    Tick delay = 0;
};

/** nidma的通信规范 */
struct CtNidmaDesc
{
    Addr remote_dpa;
    Addr local_offest;
    unsigned len;
    bool is_read;
};

/** 矩阵引擎的交互 */
/** [MxK] * [KxN] = [MxN]*/
struct CtMatmulDesc
{
    Addr a_offset;
    Addr b_offset;
    Addr dst_offset;
    unsigned M;
    unsigned N;
    unsigned K;
    bool sig_en;
    unsigned sync_id;
};

enum CtVecOp{
    add,
    mult,
    silu,
};

/** 处理数据的来源地址 */
struct CtVectorDesc
{
    CtVecOp op;

    /** 操作数 */
    /** 在我们最开始只实现的指令中 src0 仅仅来源于 fc32 */
    unsigned src0_fc32_idx;
    unsigned src1_fc32_idx;
    Addr src1_sram_idx;
    bool src1_from_fc32;

    /** 结果地址 */
    bool dst_from_fc32;
    unsigned dst_fc32_idx;
    Addr dst_sram_idx;

    /** 同步 */
    /** 需要等待矩阵引擎判断 */
    bool wait_en;
    unsigned wait_sync_id;
    uint32_t wait_val;

    bool     sig_en;              
    unsigned sig_sync_id;
    unsigned float_num;
};

//todo 定义compute tile 内部 riscfive 核的通信规范

}
}

#endif
