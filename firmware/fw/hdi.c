/**
 * @file  hdi.c
 * @brief 固件的 HDI 命令处理（CP）的实现
 *
 * 单线程、协作式。每个队列（管理队列与 16 个上下文）同时只执行一条
 * 命令，按 SQ 的顺序逐条开始（spec-hdi §10.1 E3 允许 CONCURRENT 的命令
 * 仍按顺序执行），每次只读取一条 SQE（§6.4 的预取上限之内）。
 *
 * - SQE、CQE 和 IDENTIFY、GET_LOG、TIMESTAMP 的数据经 HDMA 通道 0 在
 *   设备内存顶端的暂存区（§6.7）与 host 之间传输，固件经 NI 读写暂存
 *   区；通道 0 上的传输逐个提交并等待完成。CQE 分两次写出（§6.5）。
 * - COPY 的数据经通道 1 传输：连续缓冲按 hdi.dma_chunk 拆成多个描述符，
 *   SGL 用一个描述符（§5.6）。多个上下文的描述符共用通道 1，固件记录
 *   每个描述符属于哪个队列；错误停止时把错误归到该命令，再 RESUME。
 *   优先级高的上下文先提交，同一优先级按 ARB_WEIGHT 轮流（§10.2）。
 * - FILL 经 NI 分步写入设备内存，每步 FILL_STEP 字节。
 * - 中断合并按 §12.2 的 P、B、C 计数；合并计时与 WAIT_EVENT 的超时经
 *   irq_deadline 唤醒主循环。
 * - 控制器复位与立即关闭用 HDMA_CTRL.ABORT_ALL（§5.6）。
 *
 * LOAD_PROGRAM、UNLOAD_PROGRAM、LAUNCH 要等 Compute Tile 建模之后实现，
 * 目前以 INVALID_OPCODE 失败，IDENTIFY 的 CTX_OPS 不报告它们。
 *
 * @see spec-hdi, spec-host-dma, spec-pcie
 */

#include "hdi.h"

#include <stddef.h>
#include <stdint.h>

#include "gw100_hdma.h"
#include "gw100_mgmt.h"
#include "irq.h"

void *memset(void *dst, int c, size_t n);

/** 队列个数：管理队列（0）与上下文 n（1 + n） */
#define NQ              17
/** 上下文个数（hdi.max_contexts） */
#define MAX_CTX         16
/** 最大队列条目数减 1（MQES） */
#define MQES            4095
/** 事件计数器个数（hdi.num_events） */
#define NUM_EVENTS      64
/** 每个上下文的区间数（hdi.ctx_regions） */
#define CTX_REGIONS     8
/** 一条传输命令的最大字节数（hdi.max_transfer） */
#define MAX_TRANSFER    (256u << 20)
/** 连续缓冲的传输拆成的描述符的最大字节数（hdi.dma_chunk） */
#define DMA_CHUNK       (1u << 20)
/** SGL 段的最大字节数（hdma.sgl_segment_max_bytes） */
#define SGL_SEG_MAX     4096u
/** 错误日志的条目数（hdi.error_log_entries） */
#define ELOG_ENTRIES    64
/** 设备内存的大小（spec-arch §3.3），DPA 从 0 开始 */
#define DEVMEM_SIZE     (32ULL << 30)
/** 固件暂存区的大小（hdi.fw_staging） */
#define STAGING_SIZE    (1ULL << 20)
/** 暂存区的起始 DPA：设备内存顶端（§6.7）；也是 DEVMEM 范围的大小 */
#define STG             (DEVMEM_SIZE - STAGING_SIZE)
/** 暂存区：队列 q 的 SQE */
#define STG_SQE(q)      (STG + 64ULL * (q))
/** 暂存区：队列 q 的 CQE */
#define STG_CQE(q)      (STG + 0x800 + 16ULL * (q))
/** 暂存区：队列 q 的 TIMESTAMP 数据 */
#define STG_TS(q)       (STG + 0xC00 + 8ULL * (q))
/** 暂存区：管理命令的 4 KiB 数据缓冲 */
#define STG_DATA        (STG + 0x1000)

/** PCI Vendor ID（仿真用占位值，与配置空间相同） */
#define VENDOR_ID       0x1234
/** PCI Device ID */
#define DEVICE_ID       0x1000
/** 硬件版本：与 BAR0 的 DEV_VERSION 相同（spec-pcie §6.1） */
#define HW_VERSION      0x00000100u
/** Compute Tile 个数（spec-arch §3.2） */
#define NUM_TILES       16
/** IDENTIFY 的 ADMIN_OPS：NOP – ABORT */
#define ADMIN_OPS       0x3FFULL
/**
 * IDENTIFY 的 CTX_OPS：NOP、COPY_H2D、COPY_D2H、FILL（0x40 – 0x43）与
 * SIGNAL_EVENT、WAIT_EVENT、TIMESTAMP（0x50 – 0x52）
 */
#define CTX_OPS         (0xFULL | 7ULL << 16)

/** mtime 的频率：100 MHz */
#define MTIME_HZ        100000000u
/** mtime 的 1 us */
#define TICKS_1US       100u
/** mtime 的 10 us：INT_COALESCE.TIME 的单位 */
#define TICKS_10US      1000u
/**
 * 通道 0 上一次传输与中止的等待上限：300 ms，超过 host 读完成超时的
 * 上限 250 ms（spec-hdi §5.6）
 */
#define DMA_TIMEOUT     (MTIME_HZ / 1000 * 300)
/** 每次 hdi_poll 的工作量（开始命令、写完成记录、FILL 的一步各算 1） */
#define POLL_BUDGET     16
/** FILL 每一步写入的字节数 */
#define FILL_STEP       4096u
/** 每个 COPY 同时提交到通道 1 的描述符上限 */
#define COPY_INFLIGHT   4
/** 通道 1 的环最多的条目数 */
#define CH1_MAX         64
/** CH_CTRL：EN，WEIGHT = 1（与 gw_hdma_chan_init 相同） */
#define CH_ON           (1u | (1u << 8))

/** HDI 寄存器：HDI_VS */
#define R_VS            0x008
/** HDI 寄存器：HDI_CC */
#define R_CC            0x010
/** HDI 寄存器：HDI_CSTS */
#define R_CSTS          0x014
/** HDI 寄存器：HDI_AQA */
#define R_AQA           0x018
/** HDI 寄存器：HDI_ASQ_LO */
#define R_ASQ_LO        0x020
/** HDI 寄存器：HDI_ASQ_HI */
#define R_ASQ_HI        0x024
/** HDI 寄存器：HDI_ACQ_LO */
#define R_ACQ_LO        0x028
/** HDI 寄存器：HDI_ACQ_HI */
#define R_ACQ_HI        0x02C
/** HDI 寄存器：HDI_FATAL */
#define R_FATAL         0x030
/** HDI 寄存器：HDI_CTX_ERR */
#define R_CTX_ERR       0x034
/** 固件视角：HDI_WR_MAP */
#define R_WR_MAP        0x100
/** HDI_WR_MAP 中 HDI_CC 的位 */
#define WR_CC           (1u << (R_CC / 4))
/** HDI_CSTS.RDY */
#define CSTS_RDY        (1u << 0)
/** HDI_CSTS.CFS */
#define CSTS_CFS        (1u << 1)
/** HDI_CSTS.SHST 的值 v */
#define CSTS_SHST(v)    ((uint32_t)(v) << 2)
/** HDI_CSTS.SHST 的位 */
#define CSTS_SHST_MASK  (3u << 2)
/** HDI_CSTS.BUSY（固件的位） */
#define CSTS_BUSY       (1u << 4)
/** INT_STATUS 中管理队列与全部上下文的位 */
#define IBITS_ALL       (GW_INT_ADMIN | 0xFFFF0000u)

/** 区间权限：R */
#define PERM_R          1u
/** 区间权限：W */
#define PERM_W          2u
/** SGL 条目的 TYPE：DATA */
#define SGL_DATA        0u
/** SGL 条目的 TYPE：SEGMENT */
#define SGL_SEGMENT     1u

/** HDI_FATAL 的原因码（spec-hdi §4.1） */
enum {
    /** 管理队列 doorbell 越界 */
    FATAL_ADMIN_DB = 1,
    /** 管理队列的 DMA 访问出错 */
    FATAL_ADMIN_DMA = 2,
    /** 固件内部错误 */
    FATAL_FW = 3,
    /** 设备内部不可恢复的硬件错误 */
    FATAL_HW = 4,
    /** 初始化参数非法（HDI_AQA 中 ASQS 或 ACQS 为 0） */
    FATAL_INIT = 5,
};

/** 命令状态：SCT << 8 | SC（spec-hdi §7.3） */
enum {
    /** 成功 */
    SC_SUCCESS = 0x00,
    /** 操作码不支持，或不能在该队列上使用 */
    SC_INVALID_OPCODE = 0x01,
    /** 字段非法 */
    SC_INVALID_FIELD = 0x02,
    /** 上下文编号越界、不存在或已存在 */
    SC_INVALID_CONTEXT = 0x03,
    /** 越权 */
    SC_ACCESS_VIOLATION = 0x04,
    /** SGL 非法 */
    SC_SGL_INVALID = 0x05,
    /** 访问 host 内存出错 */
    SC_DATA_XFER = 0x06,
    /** 设备内存不可纠正的错误 */
    SC_DEVMEM = 0x07,
    /** 被 ABORT 命令中止 */
    SC_ABORTED_REQ = 0x08,
    /** 上下文进入错误状态 */
    SC_ABORTED_CTX = 0x09,
    /** 设备内部错误 */
    SC_INTERNAL = 0x0A,
    /** 等待超时 */
    SC_TIMEOUT = 0x0C,
    /** 控制器正常关闭 */
    SC_ABORTED_SHUTDOWN = 0x0D,
    /** CREATE_CONTEXT 的队列参数非法 */
    SC_QUEUE_INVALID = 0x100,
};

/** 操作码（spec-hdi §9、§10） */
enum {
    /** 管理：NOP */
    OP_NOP = 0x00,
    /** 管理：IDENTIFY */
    OP_IDENTIFY = 0x01,
    /** 管理：CREATE_CONTEXT */
    OP_CREATE_CTX = 0x02,
    /** 管理：DESTROY_CONTEXT */
    OP_DESTROY_CTX = 0x03,
    /** 管理：SET_CTX_REGION */
    OP_SET_REGION = 0x04,
    /** 管理：SET_CTX_EVENTS */
    OP_SET_EVENTS = 0x05,
    /** 管理：GET_FEATURE */
    OP_GET_FEATURE = 0x06,
    /** 管理：SET_FEATURE */
    OP_SET_FEATURE = 0x07,
    /** 管理：GET_LOG */
    OP_GET_LOG = 0x08,
    /** 管理：ABORT */
    OP_ABORT = 0x09,
    /** 上下文：NOP */
    OP_CTX_NOP = 0x40,
    /** 上下文：COPY_H2D */
    OP_COPY_H2D = 0x41,
    /** 上下文：COPY_D2H */
    OP_COPY_D2H = 0x42,
    /** 上下文：FILL */
    OP_FILL = 0x43,
    /** 上下文：SIGNAL_EVENT */
    OP_SIGNAL = 0x50,
    /** 上下文：WAIT_EVENT */
    OP_WAIT = 0x51,
    /** 上下文：TIMESTAMP */
    OP_TIMESTAMP = 0x52,
};

/** 特性编号（spec-hdi §9.7） */
enum {
    /** INT_COALESCE（按上下文） */
    FID_COALESCE = 0x01,
    /** ARB_WEIGHT（按上下文） */
    FID_WEIGHT = 0x02,
    /** DEBUG_WINDOW（全局） */
    FID_DEBUG_WIN = 0x03,
};

/** 控制器的状态（spec-hdi §5.1） */
enum ctrl_state {
    /** 未就绪：复位完成，RDY = CFS = SHST = 0 */
    CTRL_RESET,
    /** 运行 */
    CTRL_RUN,
    /** 正常关闭进行中 */
    CTRL_SHUT,
    /** 立即关闭已受理，中止在 hdi_poll 中进行 */
    CTRL_SHUT_NOW,
    /** 关闭完成 */
    CTRL_SHUT_DONE,
    /** 致命错误，或初始化、复位、关闭失败（CFS = 1） */
    CTRL_FATAL,
};

/** 上下文的状态（spec-hdi §11.1），与 IDENTIFY 的 STATE 编码相同 */
enum {
    /** 不存在 */
    Q_NONE = 0,
    /** 活动 */
    Q_ACTIVE = 1,
    /** 错误 */
    Q_ERROR = 2,
};

/** 命令的执行状态 */
enum cmd_state {
    /** 空闲：可以开始下一条命令 */
    CMD_IDLE,
    /** COPY：描述符在通道 1 上 */
    CMD_COPY,
    /** FILL：经 NI 分步写入 */
    CMD_FILL,
    /** WAIT_EVENT：等待事件计数器 */
    CMD_WAIT,
    /** DESTROY_CONTEXT：等目标上下文已提交的描述符结束 */
    CMD_DRAIN,
    /** 已完成，完成记录待写出（CQ 满时停在这里） */
    CMD_CQE,
};

/** 已提交描述符的 COPY 被取消时的处理 */
enum {
    /** 没有取消 */
    CANCEL_NONE,
    /** 不再提交，描述符都结束后以 status 完成 */
    CANCEL_STATUS,
    /** 不再提交，描述符都结束后丢弃，不写完成记录 */
    CANCEL_DROP,
};

/** 一个设备内存区间（spec-hdi §9.5） */
struct region {
    /** 基地址 */
    uint64_t base;
    /** 字节数；0 表示未设置 */
    uint64_t size;
    /** 权限：[0] R，[1] W，[2] X */
    uint32_t perm;
};

/** 一个队列上正在执行的命令 */
struct cmd {
    /** 执行状态 */
    enum cmd_state st;
    /** SQE */
    uint32_t sqe[16];
    /** 完成记录的 RESULT_LO */
    uint32_t res_lo;
    /** 完成记录的 RESULT_HI */
    uint32_t res_hi;
    /** 完成状态：SCT << 8 | SC */
    uint16_t status;
    /** COPY：第一个出错的描述符给出的状态；0 表示没有 */
    uint16_t fail;
    /** COPY：取消方式，CANCEL_* */
    uint8_t cancel;
    /** COPY：host 侧是 SGL */
    uint8_t sgl;
    /** COPY：SGL 第一段的字节数 */
    uint32_t seg;
    /** COPY：下一个描述符的 host 地址 */
    uint64_t host;
    /** COPY、FILL：下一个描述符（或下一步）的 DPA */
    uint64_t dev;
    /** COPY、FILL：尚未提交（或尚未写入）的字节数 */
    uint32_t left;
    /** COPY：已提交、尚未结束的描述符数 */
    uint32_t inflight;
    /** WAIT_EVENT：超时的 mtime；0 表示无限等待 */
    uint64_t deadline;
    /** DESTROY_CONTEXT：目标队列 */
    unsigned target;
};

/** 队列（管理队列或一个上下文）的状态 */
struct queue {
    /** 上下文的状态 Q_*；管理队列在控制器初始化后为 Q_ACTIVE */
    uint8_t state;
    /** 优先级 0 – 3 */
    uint8_t prio;
    /** ARB_WEIGHT；0 视为 1 */
    uint8_t weight;
    /** 是否使能完成中断（管理队列总是发中断） */
    uint8_t int_en;
    /** MSI-X 向量 */
    uint8_t vector;
    /** INT_COALESCE.THR；0 视为 1 */
    uint8_t thr;
    /** INT_COALESCE.TIME，单位 10 us */
    uint8_t time;
    /** 合并计时是否在进行 */
    uint8_t timing;
    /** CQ 当前的相位位 */
    uint8_t phase;
    /** SQ 的基地址 */
    uint64_t sq_base;
    /** CQ 的基地址 */
    uint64_t cq_base;
    /** SQ 的条目数 */
    uint16_t sq_n;
    /** CQ 的条目数 */
    uint16_t cq_n;
    /** SQ 头 */
    uint16_t sq_head;
    /** SQ 尾（最近一次合法的 SQ_TAIL） */
    uint16_t sq_tail;
    /** CQ 头（最近一次合法的 CQ_HEAD） */
    uint16_t cq_head;
    /** CQ 尾：下一条 CQE 的位置 */
    uint16_t cq_tail;
    /** 已发布的 CQE 数 P（§12.2） */
    uint32_t pub;
    /** 已通知的 CQE 数 B */
    uint32_t ntf;
    /** 已消费的 CQE 数 C */
    uint32_t cons;
    /** 合并计时的起点（mtime） */
    uint64_t tstart;
    /** 可使用的事件位图 */
    uint64_t events;
    /** 设备内存区间表 */
    struct region rgn[CTX_REGIONS];
    /** 正在执行的命令 */
    struct cmd cmd;
};

/** 一条错误日志（spec-hdi §9.8） */
struct elog_entry {
    /** 时间戳 */
    uint64_t ts;
    /** 上下文编号；0xFF 表示管理队列或全局 */
    uint8_t ctx;
    /** SCT */
    uint8_t sct;
    /** SC */
    uint8_t sc;
    /** OPC */
    uint8_t opc;
    /** CID */
    uint16_t cid;
};

/** 队列：0 是管理队列，1 + n 是上下文 n */
static struct queue q[NQ];
/** 通道 0：SQE、CQE 与命令的小块数据 */
static struct gw_hdma_chan *ch0;
/** 通道 1：COPY 的数据 */
static struct gw_hdma_chan *ch1;
/** 通道 1 每个环槽位上的描述符属于哪个队列 */
static uint8_t owner[CH1_MAX];
/** 事件计数器（spec-hdi §10.9） */
static uint32_t events[NUM_EVENTS];
/** 控制器的状态 */
static enum ctrl_state ctrl;
/** HDI_CSTS 中固件维护的 RDY、CFS、SHST */
static uint32_t csts;
/** HDI_CSTS 中固件的 BUSY 位 */
static uint32_t busy;
/** 固件写入 INT_LEVEL 的值 */
static uint32_t int_level;
/** HDI_CTX_ERR 的值 */
static uint32_t ctx_err;
/** 固件版本，写入 IDENTIFY */
static uint32_t fw_ver;
/** 本次 hdi_poll 剩余的工作量 */
static int budget;
/** 各优先级提交描述符的轮转起点 */
static uint8_t rr[4];
/** 错误日志，环形：最新的一条是 elog[(elog_n − 1) % ELOG_ENTRIES] */
static struct elog_entry elog[ELOG_ENTRIES];
/** 错误日志累计记录的条数 */
static uint32_t elog_n;

static void ctx_error(unsigned qi, int cq_ok);

/** 读 mtime */
static uint64_t
now(void)
{
    return gw_read64(GW_CLINT_MTIME);
}

/** 喂狗：在可能长时间等待的循环中调用 */
static void
kick(void)
{
    gw_write32(GW_WDT_KICK, GW_WDT_KICK_MAGIC);
}

/** 访存屏障：NI 与 ML 是不同的目标，相互之间没有顺序保证（§6.5） */
static void
fence(void)
{
    __asm__ volatile("fence" ::: "memory");
}

/** 经 NI 访问 DPA dpa 处的 32 位字 */
static volatile uint32_t *
ni32(uint64_t dpa)
{
    return (volatile uint32_t *)(uintptr_t)(GW_NI_BASE + dpa);
}

/** 经 NI 访问 DPA dpa 处的 64 位字 */
static volatile uint64_t *
ni64(uint64_t dpa)
{
    return (volatile uint64_t *)(uintptr_t)(GW_NI_BASE + dpa);
}

/** 读 HDI 寄存器 off（固件视角） */
static uint32_t
hdi_rd(unsigned off)
{
    return gw_read32(GW_HDI_FW(off));
}

/** 写 HDI 寄存器 off（固件视角） */
static void
hdi_wr(unsigned off, uint32_t v)
{
    gw_write32(GW_HDI_FW(off), v);
}

/** 写 HDI_CSTS：固件维护的状态位与固件的 BUSY 位 */
static void
csts_write(void)
{
    hdi_wr(R_CSTS, csts | busy);
}

/** 置位（on）或清零 INT_LEVEL 中的位 bits */
static void
level(uint32_t bits, int on)
{
    uint32_t v = on ? (int_level | bits) : (int_level & ~bits);
    if (v != int_level) {
        int_level = v;
        gw_write32(GW_INT_LEVEL, v);
    }
}

/** 队列 qi 的 INT_STATUS 位 */
static uint32_t
ibit(unsigned qi)
{
    return qi ? GW_INT_CTX(qi - 1) : GW_INT_ADMIN;
}

/** 序列号比较：a − b 解释为有符号数 */
static int32_t
sdiff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

/** 清零队列 qi 的两个 DB_PENDING 位，丢弃信箱中的旧值（§6.6） */
static void
db_clear(unsigned qi)
{
    uint64_t m = 3ULL << (2 * qi);
    gw_write32(GW_DB_PENDING0, (uint32_t)m);
    gw_write32(GW_DB_PENDING1, (uint32_t)(m >> 32));
}

/** 记一条错误日志：ctx 为上下文编号（0xFF 为管理队列或全局） */
static void
elog_add(unsigned ctx, uint16_t st, const struct cmd *c)
{
    struct elog_entry *e = &elog[elog_n % ELOG_ENTRIES];
    e->ts = now();
    e->ctx = (uint8_t)ctx;
    e->sct = (uint8_t)(st >> 8);
    e->sc = (uint8_t)st;
    e->opc = c ? (uint8_t)c->sqe[0] : 0;
    e->cid = c ? (uint16_t)(c->sqe[0] >> 16) : 0;
    elog_n++;
}

/** 状态 st 的 DNR 位（§7.3） */
static uint32_t
dnr(uint16_t st)
{
    switch (st) {
      case SC_SUCCESS:
      case SC_DATA_XFER:
      case SC_DEVMEM:
      case SC_INTERNAL:
      case SC_TIMEOUT:
        return 0;
      default:
        return 1;
    }
}

/**
 * HDMA 的错误码 e（spec-host-dma §8.1）对应的命令状态；访问 host 出错
 * 时把原因写入 *res（RESULT_LO）
 */
static uint16_t
xfer_status(int e, uint32_t *res)
{
    switch (e) {
      case 1:   /* HOST_UR */
      case 2:   /* HOST_CA */
      case 3:   /* HOST_TIMEOUT */
      case 4:   /* HOST_POISONED */
        *res = (uint32_t)e;
        return SC_DATA_XFER;
      case 6:   /* DEV_ECC */
        return SC_DEVMEM;
      case 8:   /* SGL_INVALID */
        return SC_SGL_INVALID;
      default:
        return SC_INTERNAL;
    }
}

/**
 * 控制器进入致命错误（§13）：先写 HDI_FATAL = code，再置 CFS = 1（两次
 * 都是经 ML 的 posted 写，按顺序生效，驱动看到 CFS 时原因码已经有效），
 * 停止处理
 */
static void
ctrl_fatal(uint32_t code)
{
    if (ctrl == CTRL_FATAL)
        return;
    ctrl = CTRL_FATAL;
    hdi_wr(R_FATAL, code);
    csts |= CSTS_CFS;
    csts_write();
    elog_add(0xFF, SC_INTERNAL, NULL);
}

/** 消费通道 1 中 C 到 HEAD 之间的结果，记到各自的命令；返回个数 */
static unsigned
reap1(void)
{
    unsigned head = gw_read32(GW_HDMA_CH(1, GW_CH_HEAD));
    unsigned n = 0;
    while (ch1->consumed != head) {
        unsigned k = ch1->consumed;
        ch1->consumed = (k + 1) & (ch1->entries - 1);
        struct cmd *c = &q[owner[k]].cmd;
        int e = (int)((ch1->ring[k].dw7 >> 8) & 0xFF);
        c->inflight--;
        if (e && !c->fail)
            c->fail = xfer_status(e, &c->res_lo);
        n++;
    }
    return n;
}

/**
 * 通道 i 出错之后恢复（spec-host-dma §8.2）：等它进入错误停止，消费到
 * HEAD（通道 1 把错误记到描述符所属的命令），读 CH_ERR，写 RESUME。
 * 回写失败（PHASE = 2）或等待超时时控制器进入致命错误，返回 -1
 */
static int
ch_recover(unsigned i)
{
    uint64_t t0 = now();
    while ((gw_read32(GW_HDMA_CH(i, GW_CH_STATUS)) & 3) != GW_CH_ERRSTOP) {
        if (now() - t0 > DMA_TIMEOUT) {
            ctrl_fatal(FATAL_FW);
            return -1;
        }
        kick();
    }
    if (i == 1)
        reap1();
    else
        ch0->consumed = gw_read32(GW_HDMA_CH(0, GW_CH_HEAD));
    uint32_t err = gw_read32(GW_HDMA_CH(i, GW_CH_ERR));
    if (((err >> 8) & 3) == 2) {
        ctrl_fatal(FATAL_FW);
        return -1;
    }
    gw_write32(GW_HDMA_CH(i, GW_CH_CTRL), CH_ON | GW_CH_RESUME);
    return 0;
}

/**
 * 在通道 0 上执行一次传输并等待完成。返回 HDMA 的错误码（0 为成功），
 * 出错时通道已恢复；等待超时或通道无法恢复时控制器进入致命错误，
 * 返回 -1
 */
static int
ch0_xfer(uint32_t dw0, uint32_t len, uint64_t host, uint64_t dev,
         uint32_t dw6)
{
    struct gw_hdma_desc d = {dw0, len, host, dev, dw6, 0};
    fence();
    unsigned idx = gw_hdma_put(ch0, &d);
    gw_hdma_commit(ch0);
    uint64_t t0 = now();
    for (;;) {
        unsigned head = gw_read32(GW_HDMA_CH(0, GW_CH_HEAD));
        if (head != ch0->consumed) {
            /* 通道 0 上同时只有这一个描述符 */
            ch0->consumed = head;
            int e = (int)((ch0->ring[idx].dw7 >> 8) & 0xFF);
            fence();
            if (e && ch_recover(0) != 0)
                return -1;
            return e;
        }
        if (now() - t0 > DMA_TIMEOUT) {
            ctrl_fatal(FATAL_FW);
            return -1;
        }
        kick();
    }
}

/**
 * 中止 HDMA 的全部通道，按 spec-host-dma §8.3 的握手等待完成；成功时
 * 返回 0，此时没有写请求滞留在设备内部，全部命令的描述符都已结束
 */
static int
abort_all(void)
{
    gw_write32(GW_HDMA_CTRL, 1u | GW_HDMA_ABORT_ALL);
    uint64_t t0 = now();
    for (unsigned i = 0; i < 2; i++) {
        struct gw_hdma_chan *c = i ? ch1 : ch0;
        /* 读回时 ABORT 已被处理（ML 按顺序处理同一方向的请求） */
        (void)gw_read32(GW_HDMA_CH(i, GW_CH_STATUS));
        for (;;) {
            uint32_t st = gw_read32(GW_HDMA_CH(i, GW_CH_STATUS)) & 3;
            if (st == GW_CH_ERRSTOP)
                return -1;  /* 中止的回写失败 */
            unsigned head = gw_read32(GW_HDMA_CH(i, GW_CH_HEAD));
            unsigned tail = gw_read32(GW_HDMA_CH(i, GW_CH_TAIL));
            if (st == 0 && head == tail) {
                c->consumed = head;
                c->tail = tail;
                break;
            }
            if (now() - t0 > DMA_TIMEOUT)
                return -1;
            kick();
        }
    }
    for (unsigned i = 0; i < NQ; i++)
        q[i].cmd.inflight = 0;
    return 0;
}

/** 丢弃全部队列、上下文和事件计数器（不访问 HIF） */
static void
drop_all(void)
{
    memset(q, 0, sizeof(q));
    memset(events, 0, sizeof(events));
}

/** 复位、立即关闭的中止失败（§5.6）：先写 HDI_FATAL = 4，再置 CFS = 1 */
static void
abort_failed(void)
{
    ctrl = CTRL_FATAL;
    hdi_wr(R_FATAL, FATAL_HW);
    csts |= CSTS_CFS;
    csts_write();
}

/**
 * 控制器复位（§5.4）：中止 HDMA，销毁全部上下文，清零事件计数器、
 * INT_LEVEL 中 HDI 的位和 HDI 的状态寄存器，最后清零 RDY、CFS、SHST
 */
static void
ctrl_reset(void)
{
    if (abort_all() != 0) {
        csts &= ~CSTS_RDY;
        abort_failed();
        return;
    }
    drop_all();
    level(IBITS_ALL, 0);
    ctx_err = 0;
    hdi_wr(R_CTX_ERR, 0);
    hdi_wr(R_FATAL, 0);
    gw_write32(GW_DB_PENDING0, ~0u);
    gw_write32(GW_DB_PENDING1, ~0u);
    csts = 0;
    csts_write();
    ctrl = CTRL_RESET;
}

/**
 * 控制器初始化（§5.2）：按 HDI_AQA、HDI_ASQ、HDI_ACQ 建立管理队列，
 * 指针置 0，相位从 1 开始。队列大小非法时初始化失败（HDI_FATAL = 5，
 * CFS = 1）
 */
static void
ctrl_init(void)
{
    uint32_t aqa = hdi_rd(R_AQA);
    unsigned sqs = aqa & 0xFFF;
    unsigned cqs = (aqa >> 16) & 0xFFF;
    if (sqs == 0 || cqs == 0) {
        ctrl_fatal(FATAL_INIT);
        return;
    }
    drop_all();
    struct queue *a = &q[0];
    a->state = Q_ACTIVE;
    a->sq_base = hdi_rd(R_ASQ_LO) | (uint64_t)hdi_rd(R_ASQ_HI) << 32;
    a->cq_base = hdi_rd(R_ACQ_LO) | (uint64_t)hdi_rd(R_ACQ_HI) << 32;
    a->sq_n = (uint16_t)(sqs + 1);
    a->cq_n = (uint16_t)(cqs + 1);
    a->phase = 1;
    db_clear(0);
    csts = CSTS_RDY;
    csts_write();
    ctrl = CTRL_RUN;
}

/** 按 HDI_CC 的当前值 cc 受理（§5.5） */
static void
cc_accept(uint32_t cc)
{
    unsigned shn = (cc >> 14) & 3;
    if (!(cc & 1)) {
        /* 已经处于复位完成状态时再写 EN = 0：直接受理 */
        if (ctrl != CTRL_RESET)
            ctrl_reset();
        return;
    }
    switch (ctrl) {
      case CTRL_RESET:
        ctrl_init();
        break;
      case CTRL_RUN:
        if (shn) {
            csts = (csts & ~CSTS_SHST_MASK) | CSTS_SHST(1);
            csts_write();
            ctrl = shn == 1 ? CTRL_SHUT : CTRL_SHUT_NOW;
        }
        break;
      case CTRL_SHUT:
        if (shn >= 2)
            ctrl = CTRL_SHUT_NOW;
        break;
      default:
        break;
    }
}

/**
 * HDI_WR 事件（spec-pcie §6.5）：先清零事件再读 HDI_WR_MAP。HDI_CC 的
 * 位按 §5.5 的握手处理：置位 BUSY，清零 map 中的位，按 HDI_CC 的当前值
 * 受理，再清零 BUSY；期间 host 又写了 HDI_CC 时重复
 */
static void
on_hdi_wr(void)
{
    gw_write32(GW_EVT_PENDING, GW_EVT_HDI_WR);
    uint32_t map = hdi_rd(R_WR_MAP);
    if (map & ~WR_CC)
        hdi_wr(R_WR_MAP, map & ~WR_CC);  /* AQA、ASQ、ACQ：初始化时读取 */
    while (map & WR_CC) {
        busy = CSTS_BUSY;
        csts_write();
        hdi_wr(R_WR_MAP, WR_CC);
        cc_accept(hdi_rd(R_CC));
        busy = 0;
        csts_write();
        map = hdi_rd(R_WR_MAP);
    }
}

/** 队列 s 中未通知且未消费的 CQE 个数 N（§12.2） */
static uint32_t
unnotified(const struct queue *s)
{
    uint32_t m = sdiff(s->ntf, s->cons) > 0 ? s->ntf : s->cons;
    return sdiff(s->pub, m) > 0 ? s->pub - m : 0;
}

/** 队列 s 的合并计时的到期时间 */
static uint64_t
expiry(const struct queue *s)
{
    return s->tstart + (uint64_t)s->time * TICKS_10US;
}

/**
 * 按 §12.2 判断队列 qi 是否通知：N ≥ THR、urgent（刚发布了一条尚未
 * 被消费的错误完成记录）、TIME = 0 或计时到期时，令 B = P，置位
 * INT_LEVEL，再写 INT_REQ。管理队列固定 THR = 1、TIME = 0
 */
static void
coalesce(unsigned qi, int urgent)
{
    struct queue *s = &q[qi];
    if (qi && !s->int_en)
        return;
    uint32_t n = unnotified(s);
    if (n == 0) {
        s->timing = 0;
        return;
    }
    if (!s->timing) {
        s->timing = 1;
        s->tstart = now();
    }
    unsigned thr = s->thr ? s->thr : 1;
    if (qi == 0 || urgent || s->time == 0 || n >= thr ||
        now() >= expiry(s)) {
        s->ntf = s->pub;
        s->timing = 0;
        level(ibit(qi), 1);
        gw_write32(GW_INT_REQ, ibit(qi));
    }
}

/** 队列 qi 的 CQ_HEAD 前进、C 已更新之后（§12.2 的消费） */
static void
consumed(unsigned qi)
{
    struct queue *s = &q[qi];
    if (sdiff(s->cons, s->ntf) >= 0)
        level(ibit(qi), 0);
    if (unnotified(s) == 0)
        s->timing = 0;
}

/** 队列 qi 的当前命令以状态 st 完成，完成记录待写出 */
static void
complete(unsigned qi, uint16_t st)
{
    struct cmd *c = &q[qi].cmd;
    c->status = st;
    c->st = CMD_CQE;
    if (st != SC_SUCCESS)
        elog_add(qi ? qi - 1 : 0xFF, st, c);
}

/**
 * 上下文 qi 进入错误状态（§11.2）：停止读取 SQ；中止正在执行的命令，
 * CQ 仍可写（cq_ok）时以 ABORTED_CTX_ERROR 写回其完成记录；置
 * HDI_CTX_ERR 的位（HIF 据此发出错误中断）并记录错误日志
 */
static void
ctx_error(unsigned qi, int cq_ok)
{
    struct queue *s = &q[qi];
    struct cmd *c = &s->cmd;
    if (s->state != Q_ACTIVE)
        return;
    s->state = Q_ERROR;
    switch (c->st) {
      case CMD_COPY:
        if (!cq_ok) {
            c->cancel = CANCEL_DROP;
        } else if (c->cancel != CANCEL_DROP) {
            c->cancel = CANCEL_STATUS;
            c->status = SC_ABORTED_CTX;
        }
        break;
      case CMD_FILL:
      case CMD_WAIT:
        if (cq_ok)
            complete(qi, SC_ABORTED_CTX);
        else
            c->st = CMD_IDLE;
        break;
      case CMD_CQE:
        if (!cq_ok)
            c->st = CMD_IDLE;
        break;
      default:
        break;
    }
    ctx_err |= 1u << (qi - 1);
    hdi_wr(R_CTX_ERR, ctx_err);
    elog_add(qi - 1, SC_ABORTED_CTX, NULL);
}

/**
 * doorbell 越界（§6.6）：管理队列使控制器进入致命错误，上下文进入错误
 * 状态；然后置 ERR_STATUS.DB_INVALID，驱动看到它时状态寄存器已经更新
 */
static void
db_bad(unsigned qi)
{
    if (qi == 0)
        ctrl_fatal(FATAL_ADMIN_DB);
    else
        ctx_error(qi, 1);
    gw_write32(GW_ERR_REQ, GW_ERR_DB_INVALID);
}

/**
 * 处理槽位 slot 的值 v（§6.6）：写入不存在的上下文时置 DB_INVALID；
 * 检查越界；CQ_HEAD 不得越过已提交 DW3 的 CQE。错误状态的上下文只
 * 处理 CQ_HEAD（其中止的命令可能还要写出完成记录）
 */
static void
doorbell(unsigned slot, unsigned v)
{
    unsigned qi = slot / 2;
    struct queue *s = &q[qi];
    if (s->state == Q_NONE) {
        if (qi)
            gw_write32(GW_ERR_REQ, GW_ERR_DB_INVALID);
        return;
    }
    if (slot & 1) {
        unsigned n = s->cq_n;
        if (v >= n) {
            db_bad(qi);
            return;
        }
        unsigned adv = (v + n - s->cq_head) % n;
        unsigned out = (s->cq_tail + n - s->cq_head) % n;
        if (adv > out) {
            db_bad(qi);
            return;
        }
        s->cq_head = (uint16_t)v;
        s->cons += adv;
        consumed(qi);
    } else if (s->state == Q_ACTIVE) {
        if (v >= s->sq_n) {
            db_bad(qi);
            return;
        }
        s->sq_tail = (uint16_t)v;
    }
}

/**
 * 写出队列 qi 的完成记录（§6.5）：在暂存区组好，经通道 0 先写 DW0 –
 * DW2，成功后再写 DW3。CQ 满时返回 0，命令保持待写；写出或失败时返回 1
 */
static int
post_cqe(unsigned qi)
{
    struct queue *s = &q[qi];
    struct cmd *c = &s->cmd;
    if ((s->cq_tail + 1) % s->cq_n == s->cq_head)
        return 0;
    uint64_t dpa = STG_CQE(qi);
    uint64_t host = s->cq_base + 16ULL * s->cq_tail;
    *ni32(dpa) = c->res_lo;
    *ni32(dpa + 4) = c->res_hi;
    *ni32(dpa + 8) = s->sq_head;
    *ni32(dpa + 12) = (c->sqe[0] >> 16) | (uint32_t)s->phase << 16 |
                      (uint32_t)(c->status & 0xFF) << 17 |
                      (uint32_t)(c->status >> 8) << 25 |
                      dnr(c->status) << 31;
    int e = ch0_xfer(GW_DESC_D2H, 12, host, dpa, 0);
    if (e == 0)
        e = ch0_xfer(GW_DESC_D2H | GW_DESC_FENCE, 4, host + 12, dpa + 12,
                     0);
    c->st = CMD_IDLE;
    if (e) {
        if (e > 0 && qi == 0)
            ctrl_fatal(FATAL_ADMIN_DMA);
        else if (e > 0)
            ctx_error(qi, 0);
        return 1;
    }
    if (++s->cq_tail == s->cq_n) {
        s->cq_tail = 0;
        s->phase ^= 1;
    }
    s->pub++;
    coalesce(qi, c->status != SC_SUCCESS && sdiff(s->cons, s->pub) < 0);
    return 1;
}

/** 读取队列 qi 的下一条 SQE（§6.4）；失败返回 -1 */
static int
fetch(unsigned qi)
{
    struct queue *s = &q[qi];
    uint64_t dpa = STG_SQE(qi);
    int e = ch0_xfer(GW_DESC_H2D, 64, s->sq_base + 64ULL * s->sq_head, dpa,
                     0);
    if (e) {
        if (e > 0 && qi == 0)
            ctrl_fatal(FATAL_ADMIN_DMA);
        else if (e > 0)
            ctx_error(qi, 1);
        return -1;
    }
    for (unsigned i = 0; i < 16; i += 2) {
        uint64_t v = *ni64(dpa + 4 * i);
        s->cmd.sqe[i] = (uint32_t)v;
        s->cmd.sqe[i + 1] = (uint32_t)(v >> 32);
    }
    if (++s->sq_head == s->sq_n)
        s->sq_head = 0;
    return 0;
}

/** SQE 的 DW[from] – DW[to] 是否全为 0 */
static int
zero(const uint32_t *s, unsigned from, unsigned to)
{
    for (unsigned i = from; i <= to; i++) {
        if (s[i])
            return 0;
    }
    return 1;
}

/** [dev, dev + len) 是否整个落在队列 s 的一个带权限 perm 的区间内 */
static int
in_region(const struct queue *s, uint64_t dev, uint64_t len, uint32_t perm)
{
    for (unsigned r = 0; r < CTX_REGIONS; r++) {
        const struct region *g = &s->rgn[r];
        if (g->size && (g->perm & perm) == perm && dev >= g->base &&
            dev - g->base <= g->size && len <= g->size - (dev - g->base))
            return 1;
    }
    return 0;
}

/**
 * 检查 SQE s 的 DPTR 用于传输 len 字节（§8）：DATA 条目要求地址按 align
 * 字节对齐、长度等于 len；allow_sgl 时还接受 SEGMENT 条目（S1），*sgl
 * 返回是否为 SEGMENT
 */
static uint16_t
dptr_check(const uint32_t *s, uint32_t len, unsigned align, int allow_sgl,
           int *sgl)
{
    uint64_t addr = s[6] | (uint64_t)s[7] << 32;
    uint32_t n = s[8];
    unsigned type = s[9] & 0xFF;
    *sgl = 0;
    if (s[9] >> 8)
        return SC_SGL_INVALID;
    if (type == SGL_DATA)
        return (addr % align || n != len) ? SC_SGL_INVALID : SC_SUCCESS;
    if (type == SGL_SEGMENT && allow_sgl) {
        *sgl = 1;
        return (addr % 16 || n % 16 || n == 0 || n > SGL_SEG_MAX)
               ? SC_SGL_INVALID : SC_SUCCESS;
    }
    return SC_SGL_INVALID;
}

/** 写暂存区数据缓冲偏移 off 处的 1 字节 */
static void
put8(unsigned off, uint8_t v)
{
    *(volatile uint8_t *)(uintptr_t)(GW_NI_BASE + STG_DATA + off) = v;
}

/** 写暂存区数据缓冲偏移 off 处的 2 字节 */
static void
put16(unsigned off, uint16_t v)
{
    *(volatile uint16_t *)(uintptr_t)(GW_NI_BASE + STG_DATA + off) = v;
}

/** 写暂存区数据缓冲偏移 off 处的 4 字节 */
static void
put32(unsigned off, uint32_t v)
{
    *ni32(STG_DATA + off) = v;
}

/** 写暂存区数据缓冲偏移 off 处的 8 字节 */
static void
put64(unsigned off, uint64_t v)
{
    *ni64(STG_DATA + off) = v;
}

/** 清零暂存区的 4 KiB 数据缓冲 */
static void
data_clear(void)
{
    for (unsigned i = 0; i < 4096; i += 8)
        put64(i, 0);
}

/**
 * 把暂存区数据缓冲的前 len 字节经通道 0 写到 SQE 的 DPTR 指向的 host
 * 缓冲（sgl 时 DPTR 是 SGL 的第一段），返回命令状态
 */
static uint16_t
data_out(struct cmd *c, uint32_t len, int sgl)
{
    const uint32_t *s = c->sqe;
    uint64_t addr = s[6] | (uint64_t)s[7] << 32;
    int e = ch0_xfer(GW_DESC_D2H | (sgl ? GW_DESC_SGL : 0), len, addr,
                     STG_DATA, sgl ? s[8] : 0);
    if (e < 0)
        return SC_INTERNAL;
    return e ? xfer_status(e, &c->res_lo) : SC_SUCCESS;
}

/** 在暂存区组好 IDENTIFY 的设备信息（CNS = 0，§9.2） */
static void
identify_dev(void)
{
    static const char hex[] = "0123456789abcdef";
    static const char name[] = "gw100-fw ";
    data_clear();
    put16(0, VENDOR_ID);
    put16(2, DEVICE_ID);
    put32(4, hdi_rd(R_VS));
    unsigned i = 0;
    for (; name[i]; i++)
        put8(8 + i, (uint8_t)name[i]);
    for (unsigned k = 0; k < 8; k++)
        put8(8 + i + k, (uint8_t)hex[(fw_ver >> (28 - 4 * k)) & 0xF]);
    put32(40, HW_VERSION);
    put32(44, MTIME_HZ);
    put64(48, 0);           /* DEVMEM_BASE */
    put64(56, STG);         /* DEVMEM_SIZE：不含暂存区 */
    put16(64, MAX_CTX);
    put16(66, MQES);
    put16(68, NUM_EVENTS);
    put8(70, CTX_REGIONS);
    /* MAX_PROGRAMS、MAX_ARG_BYTES、FEATURES 为 0：程序相关的命令尚未
     * 实现，CONCURRENT 的命令按顺序执行 */
    put32(72, MAX_TRANSFER);
    put16(80, SGL_SEG_MAX);
    put16(82, NUM_TILES);
    put64(88, ADMIN_OPS);
    put64(96, CTX_OPS);
}

/** 在暂存区组好 IDENTIFY 的上下文 id 的信息（CNS = 1，§9.2） */
static void
identify_ctx(unsigned id)
{
    const struct queue *s = &q[1 + id];
    data_clear();
    put8(0, (uint8_t)id);
    put8(1, s->state);
    if (s->state == Q_NONE)
        return;
    put8(2, s->vector);
    put8(3, s->prio);
    put16(4, (uint16_t)(s->sq_n - 1));
    put16(6, (uint16_t)(s->cq_n - 1));
    put64(8, s->events);
    for (unsigned r = 0; r < CTX_REGIONS; r++) {
        put64(32 + 24 * r, s->rgn[r].base);
        put64(40 + 24 * r, s->rgn[r].size);
        put32(48 + 24 * r, s->rgn[r].perm);
    }
}

/** IDENTIFY（§9.2） */
static uint16_t
cmd_identify(struct cmd *c)
{
    const uint32_t *s = c->sqe;
    unsigned cns = s[10] & 0xFF;
    unsigned id = (s[10] >> 8) & 0xFF;
    int sgl;
    if ((s[10] >> 16) || !zero(s, 11, 15) || cns > 1 || (cns == 0 && id))
        return SC_INVALID_FIELD;
    if (cns == 1 && id >= MAX_CTX)
        return SC_INVALID_CONTEXT;
    uint16_t st = dptr_check(s, 4096, 4, 0, &sgl);
    if (st)
        return st;
    if (cns == 0)
        identify_dev();
    else
        identify_ctx(id);
    return data_out(c, 4096, sgl);
}

/** CREATE_CONTEXT（§9.3） */
static uint16_t
cmd_create(const uint32_t *s)
{
    unsigned id = s[10] & 0xFF;
    unsigned sqs = s[11] & 0xFFFF;
    unsigned cqs = s[11] >> 16;
    uint64_t sqb = s[12] | (uint64_t)s[13] << 32;
    uint64_t cqb = s[14] | (uint64_t)s[15] << 32;
    if ((s[10] & 0xFEFCE000u) || !zero(s, 6, 9))
        return SC_INVALID_FIELD;
    if (id >= MAX_CTX || q[1 + id].state != Q_NONE)
        return SC_INVALID_CONTEXT;
    /* VECTOR 只有 5 位，总小于 MSI-X 表项数（32），不会以
     * VECTOR_INVALID 失败 */
    if (sqs == 0 || sqs > MQES || cqs == 0 || cqs > MQES ||
        (sqb & 0xFFF) || (cqb & 0xFFF))
        return SC_QUEUE_INVALID;
    unsigned qi = 1 + id;
    struct queue *t = &q[qi];
    db_clear(qi);
    gw_write32(GW_CTX_VECTOR(id), (s[10] >> 8) & 0x1F);
    memset(t, 0, sizeof(*t));
    t->state = Q_ACTIVE;
    t->vector = (uint8_t)((s[10] >> 8) & 0x1F);
    t->prio = (uint8_t)((s[10] >> 16) & 3);
    t->int_en = (uint8_t)((s[10] >> 24) & 1);
    t->weight = 1;
    t->thr = 1;
    t->sq_base = sqb;
    t->cq_base = cqb;
    t->sq_n = (uint16_t)(sqs + 1);
    t->cq_n = (uint16_t)(cqs + 1);
    t->phase = 1;
    return SC_SUCCESS;
}

/**
 * DESTROY_CONTEXT（§9.4）：停止读取 SQ，中止该上下文的命令（不写完成
 * 记录），等它已提交的描述符结束（CMD_DRAIN），再清除上下文并完成
 */
static void
cmd_destroy(struct cmd *c)
{
    const uint32_t *s = c->sqe;
    unsigned id = s[10] & 0xFF;
    if ((s[10] >> 8) || !zero(s, 6, 9) || !zero(s, 11, 15)) {
        complete(0, SC_INVALID_FIELD);
        return;
    }
    if (id >= MAX_CTX || q[1 + id].state == Q_NONE) {
        complete(0, SC_INVALID_CONTEXT);
        return;
    }
    unsigned qi = 1 + id;
    struct cmd *t = &q[qi].cmd;
    q[qi].state = Q_NONE;  /* 之后的 doorbell 按不存在的上下文处理 */
    if (t->st == CMD_COPY)
        t->cancel = CANCEL_DROP;
    else
        t->st = CMD_IDLE;
    c->target = qi;
    c->st = CMD_DRAIN;
}

/** DESTROY_CONTEXT 的最后一步：清除上下文 qi 的全部状态 */
static void
destroy_finish(unsigned qi)
{
    memset(&q[qi], 0, sizeof(q[qi]));
    ctx_err &= ~(1u << (qi - 1));
    hdi_wr(R_CTX_ERR, ctx_err);
    level(ibit(qi), 0);
    db_clear(qi);
}

/** SET_CTX_REGION（§9.5） */
static uint16_t
cmd_set_region(const uint32_t *s)
{
    unsigned id = s[10] & 0xFF;
    unsigned r = (s[10] >> 8) & 0xF;
    uint64_t base = s[12] | (uint64_t)s[13] << 32;
    uint64_t size = s[14] | (uint64_t)s[15] << 32;
    if ((s[10] & 0xFFF8F000u) || s[11] || !zero(s, 6, 9))
        return SC_INVALID_FIELD;
    if (id >= MAX_CTX || q[1 + id].state != Q_ACTIVE)
        return SC_INVALID_CONTEXT;
    if (r >= CTX_REGIONS || (base & 0xFFF) || (size & 0xFFF))
        return SC_INVALID_FIELD;
    /* DEVMEM 范围是 [0, STG)：整个区间落在其中，按 64 位不回绕 */
    if (size && (base >= STG || size > STG - base))
        return SC_INVALID_FIELD;
    struct region *g = &q[1 + id].rgn[r];
    g->base = base;
    g->size = size;
    g->perm = (s[10] >> 16) & 7;
    return SC_SUCCESS;
}

/** SET_CTX_EVENTS（§9.6） */
static uint16_t
cmd_set_events(const uint32_t *s)
{
    unsigned id = s[10] & 0xFF;
    if ((s[10] >> 8) || s[11] || s[14] || s[15] || !zero(s, 6, 9))
        return SC_INVALID_FIELD;
    if (id >= MAX_CTX || q[1 + id].state != Q_ACTIVE)
        return SC_INVALID_CONTEXT;
    q[1 + id].events = s[12] | (uint64_t)s[13] << 32;
    return SC_SUCCESS;
}

/** GET_FEATURE 与 SET_FEATURE（§9.7） */
static uint16_t
cmd_feature(struct cmd *c)
{
    const uint32_t *s = c->sqe;
    int set = (s[0] & 0xFF) == OP_SET_FEATURE;
    unsigned fid = s[10] & 0xFF;
    unsigned id = (s[10] >> 8) & 0xFF;
    uint32_t v = s[11];
    if ((s[10] >> 16) || !zero(s, 6, 9) || !zero(s, 12, 15) || (!set && v))
        return SC_INVALID_FIELD;
    if (fid == FID_DEBUG_WIN) {
        if (!set) {
            c->res_lo = gw_read32(GW_DEV_CTRL_FW) & 1;
            return SC_SUCCESS;
        }
        if (v >> 1)
            return SC_INVALID_FIELD;
        gw_write32(GW_DEV_CTRL_FW, v);
        /* 读回：新状态已经生效；熔丝禁止时写入无效 */
        if ((gw_read32(GW_DEV_CTRL_FW) & 1) != v)
            return SC_ACCESS_VIOLATION;
        return SC_SUCCESS;
    }
    if (fid != FID_COALESCE && fid != FID_WEIGHT)
        return SC_INVALID_FIELD;
    if (id >= MAX_CTX || q[1 + id].state != Q_ACTIVE)
        return SC_INVALID_CONTEXT;
    struct queue *t = &q[1 + id];
    if (fid == FID_COALESCE) {
        if (!set) {
            c->res_lo = t->thr | (uint32_t)t->time << 8;
            return SC_SUCCESS;
        }
        if (v >> 16)
            return SC_INVALID_FIELD;
        t->thr = (uint8_t)v;
        t->time = (uint8_t)(v >> 8);
        coalesce(1 + id, 0);  /* 按新的 THR、TIME 重新判断当前的 N */
        return SC_SUCCESS;
    }
    if (!set) {
        c->res_lo = t->weight;
        return SC_SUCCESS;
    }
    if (v >> 4)
        return SC_INVALID_FIELD;
    t->weight = (uint8_t)v;
    return SC_SUCCESS;
}

/** GET_LOG（§9.8）：只支持错误日志（LID 1） */
static uint16_t
cmd_get_log(struct cmd *c)
{
    const uint32_t *s = c->sqe;
    uint32_t n = s[11];
    int sgl;
    if ((s[10] >> 8) || !zero(s, 12, 15) || (s[10] & 0xFF) != 1 ||
        n == 0 || n % 4 || n > 4096)
        return SC_INVALID_FIELD;
    uint16_t st = dptr_check(s, n, 4, 1, &sgl);
    if (st)
        return st;
    data_clear();
    unsigned cnt = elog_n < ELOG_ENTRIES ? elog_n : ELOG_ENTRIES;
    for (unsigned i = 0; i < cnt && 64 * i < n; i++) {
        const struct elog_entry *e =
            &elog[(elog_n - 1 - i) % ELOG_ENTRIES];
        unsigned off = 64 * i;
        put64(off, e->ts);
        put8(off + 8, e->ctx);
        put8(off + 9, e->sct);
        put8(off + 10, e->sc);
        put8(off + 11, e->opc);
        put16(off + 12, e->cid);
    }
    return data_out(c, n, sgl);
}

/**
 * ABORT（§9.9）：目标是上下文正在执行的 COPY、FILL 或 WAIT_EVENT 时
 * 中止它，RESULT_LO = 1；目标已完成或不存在时仍然成功
 */
static uint16_t
cmd_abort(struct cmd *c)
{
    const uint32_t *s = c->sqe;
    unsigned id = s[10] & 0xFF;
    unsigned cid = s[10] >> 16;
    if ((s[10] & 0xFF00) || !zero(s, 6, 9) || !zero(s, 11, 15))
        return SC_INVALID_FIELD;
    if (id >= MAX_CTX || q[1 + id].state == Q_NONE)
        return SC_INVALID_CONTEXT;
    unsigned qi = 1 + id;
    struct cmd *t = &q[qi].cmd;
    if (q[qi].state != Q_ACTIVE || (t->sqe[0] >> 16) != cid)
        return SC_SUCCESS;
    switch (t->st) {
      case CMD_COPY:
        if (t->cancel || t->fail)
            return SC_SUCCESS;
        t->cancel = CANCEL_STATUS;
        t->status = SC_ABORTED_REQ;
        break;
      case CMD_FILL:
      case CMD_WAIT:
        complete(qi, SC_ABORTED_REQ);
        break;
      default:
        return SC_SUCCESS;
    }
    c->res_lo = 1;
    return SC_SUCCESS;
}

/** 执行管理队列的当前命令（§9）：完成，或进入等待状态 */
static void
admin_exec(void)
{
    struct cmd *c = &q[0].cmd;
    const uint32_t *s = c->sqe;
    unsigned opc = s[0] & 0xFF;
    if (opc > OP_ABORT) {
        complete(0, SC_INVALID_OPCODE);
        return;
    }
    if ((s[0] & 0xF800) || !zero(s, 1, 5)) {
        complete(0, SC_INVALID_FIELD);
        return;
    }
    switch (opc) {
      case OP_NOP:
        complete(0, zero(s, 6, 15) ? SC_SUCCESS : SC_INVALID_FIELD);
        break;
      case OP_IDENTIFY:
        complete(0, cmd_identify(c));
        break;
      case OP_CREATE_CTX:
        complete(0, cmd_create(s));
        break;
      case OP_DESTROY_CTX:
        cmd_destroy(c);
        break;
      case OP_SET_REGION:
        complete(0, cmd_set_region(s));
        break;
      case OP_SET_EVENTS:
        complete(0, cmd_set_events(s));
        break;
      case OP_GET_FEATURE:
      case OP_SET_FEATURE:
        complete(0, cmd_feature(c));
        break;
      case OP_GET_LOG:
        complete(0, cmd_get_log(c));
        break;
      default:
        complete(0, cmd_abort(c));
        break;
    }
}

/**
 * COPY、FILL 的 DEV_ADDR 与 LENGTH（CDW12 – CDW14，§10.3）：4 B 对齐、
 * 长度 1 – hdi.max_transfer，整个范围落在一个带权限 perm 的区间内
 */
static uint16_t
range_check(const struct queue *t, const uint32_t *s, uint32_t perm,
            uint64_t *dev, uint32_t *len)
{
    *dev = s[12] | (uint64_t)s[13] << 32;
    *len = s[14];
    if ((*dev & 3) || (*len & 3) || *len == 0 || *len > MAX_TRANSFER)
        return SC_INVALID_FIELD;
    if (!in_region(t, *dev, *len, perm))
        return SC_ACCESS_VIOLATION;
    return SC_SUCCESS;
}

/** COPY_H2D、COPY_D2H（§10.3）：检查后交给 submit() 分段提交 */
static void
cmd_copy(unsigned qi)
{
    struct queue *t = &q[qi];
    struct cmd *c = &t->cmd;
    const uint32_t *s = c->sqe;
    int h2d = (s[0] & 0xFF) == OP_COPY_H2D;
    uint64_t dev;
    uint32_t len;
    int sgl;
    if (s[10] || s[11] || s[15]) {
        complete(qi, SC_INVALID_FIELD);
        return;
    }
    uint16_t st = range_check(t, s, h2d ? PERM_W : PERM_R, &dev, &len);
    if (!st)
        st = dptr_check(s, len, 4, 1, &sgl);
    if (st) {
        complete(qi, st);
        return;
    }
    c->host = s[6] | (uint64_t)s[7] << 32;
    c->dev = dev;
    c->left = len;
    c->sgl = (uint8_t)sgl;
    c->seg = s[8];
    c->inflight = 0;
    c->fail = 0;
    c->cancel = CANCEL_NONE;
    c->st = CMD_COPY;
}

/** FILL（§10.4）：检查后由 fill_step() 分步写入 */
static void
cmd_fill(unsigned qi)
{
    struct queue *t = &q[qi];
    struct cmd *c = &t->cmd;
    const uint32_t *s = c->sqe;
    uint64_t dev;
    uint32_t len;
    if (s[10] || s[11] || !zero(s, 6, 9)) {
        complete(qi, SC_INVALID_FIELD);
        return;
    }
    uint16_t st = range_check(t, s, PERM_W, &dev, &len);
    if (st) {
        complete(qi, st);
        return;
    }
    c->dev = dev;
    c->left = len;
    c->st = CMD_FILL;
}

/** FILL 的一步：经 NI 写入至多 FILL_STEP 字节的图样 */
static void
fill_step(unsigned qi)
{
    struct cmd *c = &q[qi].cmd;
    uint32_t pat = c->sqe[15];
    uint64_t pat2 = pat | (uint64_t)pat << 32;
    uint32_t n = c->left < FILL_STEP ? c->left : FILL_STEP;
    uint64_t d = c->dev;
    uint32_t i = 0;
    if ((d & 4) && n >= 4) {
        *ni32(d) = pat;
        i = 4;
    }
    for (; i + 8 <= n; i += 8)
        *ni64(d + i) = pat2;
    if (i < n)
        *ni32(d + i) = pat;
    c->dev += n;
    c->left -= n;
    if (!c->left) {
        fence();
        complete(qi, SC_SUCCESS);
    }
}

/** SIGNAL_EVENT（§10.8） */
static uint16_t
cmd_signal(const struct queue *t, const uint32_t *s)
{
    unsigned e = s[10] & 0xFF;
    unsigned op = (s[10] >> 8) & 3;
    if ((s[10] >> 10) || !zero(s, 6, 9) || !zero(s, 12, 15) ||
        e >= NUM_EVENTS || op > 1)
        return SC_INVALID_FIELD;
    if (!((t->events >> e) & 1))
        return SC_ACCESS_VIOLATION;
    events[e] = op ? events[e] + s[11] : s[11];
    return SC_SUCCESS;
}

/** WAIT_EVENT（§10.9）：检查后由 step() 等待计数器或超时 */
static void
cmd_wait(unsigned qi)
{
    struct queue *t = &q[qi];
    struct cmd *c = &t->cmd;
    const uint32_t *s = c->sqe;
    unsigned e = s[10] & 0xFF;
    if ((s[10] >> 8) || !zero(s, 6, 9) || !zero(s, 13, 15) ||
        e >= NUM_EVENTS) {
        complete(qi, SC_INVALID_FIELD);
        return;
    }
    if (!((t->events >> e) & 1)) {
        complete(qi, SC_ACCESS_VIOLATION);
        return;
    }
    c->deadline = s[12] ? now() + (uint64_t)s[12] * TICKS_1US : 0;
    c->st = CMD_WAIT;
}

/** TIMESTAMP（§10.10）：开始执行时读设备时间戳，写入 host 缓冲 */
static uint16_t
cmd_timestamp(unsigned qi)
{
    struct cmd *c = &q[qi].cmd;
    const uint32_t *s = c->sqe;
    int sgl;
    if (!zero(s, 10, 15))
        return SC_INVALID_FIELD;
    uint16_t st = dptr_check(s, 8, 8, 0, &sgl);
    if (st)
        return st;
    *ni64(STG_TS(qi)) = now();
    int e = ch0_xfer(GW_DESC_D2H, 8, s[6] | (uint64_t)s[7] << 32,
                     STG_TS(qi), 0);
    if (e < 0)
        return SC_INTERNAL;
    return e ? xfer_status(e, &c->res_lo) : SC_SUCCESS;
}

/** 执行上下文 qi 的当前命令（§10）：完成，或进入执行状态 */
static void
ctx_exec(unsigned qi)
{
    struct queue *t = &q[qi];
    struct cmd *c = &t->cmd;
    const uint32_t *s = c->sqe;
    unsigned opc = s[0] & 0xFF;
    switch (opc) {
      case OP_CTX_NOP:
      case OP_COPY_H2D:
      case OP_COPY_D2H:
      case OP_FILL:
      case OP_SIGNAL:
      case OP_WAIT:
      case OP_TIMESTAMP:
        break;
      default:
        complete(qi, SC_INVALID_OPCODE);
        return;
    }
    if ((s[0] & 0xF800) || !zero(s, 1, 5)) {
        complete(qi, SC_INVALID_FIELD);
        return;
    }
    switch (opc) {
      case OP_CTX_NOP:
        complete(qi, zero(s, 6, 15) ? SC_SUCCESS : SC_INVALID_FIELD);
        break;
      case OP_COPY_H2D:
      case OP_COPY_D2H:
        cmd_copy(qi);
        break;
      case OP_FILL:
        cmd_fill(qi);
        break;
      case OP_SIGNAL:
        complete(qi, cmd_signal(t, s));
        break;
      case OP_WAIT:
        cmd_wait(qi);
        break;
      default:
        complete(qi, cmd_timestamp(qi));
        break;
    }
}

/** 队列 qi 是否可以开始下一条命令 */
static int
startable(unsigned qi)
{
    const struct queue *t = &q[qi];
    return t->state == Q_ACTIVE && t->cmd.st == CMD_IDLE &&
           t->sq_head != t->sq_tail;
}

/** 读取并开始队列 qi 的下一条命令 */
static void
start(unsigned qi)
{
    struct cmd *c = &q[qi].cmd;
    if (fetch(qi) != 0)
        return;
    c->res_lo = 0;
    c->res_hi = 0;
    c->status = SC_SUCCESS;
    if (qi == 0)
        admin_exec();
    else
        ctx_exec(qi);
}

/**
 * 开始各队列的下一条命令：管理队列先，上下文按优先级从高到低；每条
 * 消耗一个工作量。返回开始的条数
 */
static int
start_cmds(void)
{
    int n = 0;
    if (startable(0) && budget > 0) {
        budget--;
        start(0);
        n++;
    }
    for (int p = 3; p >= 0; p--) {
        for (unsigned i = 1; i < NQ && budget > 0; i++) {
            if (ctrl != CTRL_RUN)
                return n;
            if (q[i].prio == p && startable(i)) {
                budget--;
                start(i);
                n++;
            }
        }
    }
    return n;
}

/** COPY 是否可以再向通道 1 提交描述符 */
static int
copy_ready(const struct cmd *c)
{
    return c->st == CMD_COPY && c->left && !c->cancel && !c->fail &&
           c->inflight < COPY_INFLIGHT;
}

/**
 * 为队列 qi 的 COPY 写入一个描述符（不提交）：连续缓冲至多
 * hdi.dma_chunk 字节，SGL 一次写完
 */
static void
put_chunk(unsigned qi)
{
    struct cmd *c = &q[qi].cmd;
    int h2d = (c->sqe[0] & 0xFF) == OP_COPY_H2D;
    uint32_t dw0 = (h2d ? GW_DESC_H2D : GW_DESC_D2H) | GW_DESC_NOTIFY;
    uint32_t len = c->left;
    uint32_t dw6 = 0;
    if (c->sgl) {
        dw0 |= GW_DESC_SGL;
        dw6 = c->seg;
    } else if (len > DMA_CHUNK) {
        len = DMA_CHUNK;
    }
    struct gw_hdma_desc d = {dw0, len, c->host, c->dev, dw6, 0};
    unsigned idx = gw_hdma_put(ch1, &d);
    owner[idx] = (uint8_t)qi;
    c->inflight++;
    c->host += len;
    c->dev += len;
    c->left -= len;
}

/**
 * 为进行中的 COPY 向通道 1 提交描述符（§10.2）：优先级高的上下文先
 * 提交；同一优先级内从轮转起点开始，每轮每个上下文至多提交 ARB_WEIGHT
 * 个。返回是否提交了描述符
 */
static int
submit(void)
{
    unsigned mask = ch1->entries - 1;
    unsigned room = mask - ((ch1->tail - ch1->consumed) & mask);
    int any = 0;
    for (int p = 3; p >= 0 && room; p--) {
        int more = 1;
        while (more && room) {
            more = 0;
            for (unsigned i = 0; i < MAX_CTX && room; i++) {
                unsigned qi = 1 + (rr[p] + i) % MAX_CTX;
                struct queue *t = &q[qi];
                if (t->prio != p || !copy_ready(&t->cmd))
                    continue;
                unsigned w = t->weight ? t->weight : 1;
                for (unsigned k = 0; k < w && room && copy_ready(&t->cmd);
                     k++) {
                    put_chunk(qi);
                    room--;
                    any = 1;
                }
                if (copy_ready(&t->cmd))
                    more = 1;
            }
            rr[p] = (uint8_t)((rr[p] + 1) % MAX_CTX);
        }
    }
    if (any)
        gw_hdma_commit(ch1);
    return any;
}

/** 推进队列 qi 的当前命令一步；返回是否有进展 */
static int
step(unsigned qi)
{
    struct cmd *c = &q[qi].cmd;
    if (ctrl != CTRL_RUN && ctrl != CTRL_SHUT)
        return 0;
    switch (c->st) {
      case CMD_COPY:
        if (c->inflight || (c->left && !c->cancel && !c->fail))
            return 0;
        if (c->cancel == CANCEL_DROP) {
            c->st = CMD_IDLE;
        } else if (c->cancel) {
            c->res_lo = 0;
            complete(qi, c->status);
        } else {
            complete(qi, c->fail);
        }
        return 1;
      case CMD_FILL:
        if (budget <= 0)
            return 0;
        budget--;
        fill_step(qi);
        return 1;
      case CMD_WAIT: {
        unsigned e = c->sqe[10] & 0xFF;
        if (sdiff(events[e], c->sqe[11]) >= 0)
            complete(qi, SC_SUCCESS);
        else if (ctrl == CTRL_SHUT)
            complete(qi, SC_ABORTED_SHUTDOWN);
        else if (c->deadline && now() >= c->deadline)
            complete(qi, SC_TIMEOUT);
        else
            return 0;
        return 1;
      }
      case CMD_DRAIN:
        if (q[c->target].cmd.inflight)
            return 0;
        destroy_finish(c->target);
        complete(0, SC_SUCCESS);
        return 1;
      case CMD_CQE:
        if (budget <= 0)
            return 0;
        budget--;
        return post_cqe(qi);
      default:
        return 0;
    }
}

/**
 * 正常关闭（§5.3）：等待中的 WAIT_EVENT 已由 step() 以 ABORTED_SHUTDOWN
 * 完成；没有未完成的命令时清零 INT_LEVEL 中 HDI 的位，置 SHST = 10
 */
static void
shut_check(void)
{
    for (unsigned i = 0; i < NQ; i++) {
        if (q[i].cmd.st != CMD_IDLE)
            return;
    }
    for (unsigned i = 0; i < NQ; i++)
        q[i].timing = 0;
    level(IBITS_ALL, 0);
    csts = (csts & ~CSTS_SHST_MASK) | CSTS_SHST(2);
    csts_write();
    ctrl = CTRL_SHUT_DONE;
}

/**
 * 立即关闭（§5.3、§5.6）：中止 HDMA，丢弃全部命令（不写完成记录），
 * 清零 INT_LEVEL 中 HDI 的位，置 SHST = 10；中止失败时 CFS = 1
 */
static void
shut_now(void)
{
    if (abort_all() != 0) {
        abort_failed();
        return;
    }
    for (unsigned i = 0; i < NQ; i++) {
        q[i].cmd.st = CMD_IDLE;
        q[i].timing = 0;
    }
    level(IBITS_ALL, 0);
    csts = (csts & ~CSTS_SHST_MASK) | CSTS_SHST(2);
    csts_write();
    ctrl = CTRL_SHUT_DONE;
}

/** 合并计时到期的队列发出通知；按计时与 WAIT_EVENT 设置唤醒期限 */
static void
timers(void)
{
    uint64_t next = 0;
    if (ctrl == CTRL_RUN || ctrl == CTRL_SHUT) {
        for (unsigned i = 0; i < NQ; i++) {
            struct queue *t = &q[i];
            if (t->timing && now() >= expiry(t))
                coalesce(i, 0);
            if (t->timing && (!next || expiry(t) < next))
                next = expiry(t);
            uint64_t d = t->cmd.deadline;
            if (t->cmd.st == CMD_WAIT && d && (!next || d < next))
                next = d;
        }
    }
    irq_deadline(next);
}

/**
 * 初始化：ch0 为队列与完成记录使用的通道 0，ch1 为数据通道（都已
 * 初始化）；fw_version 写入 IDENTIFY。控制器处于未就绪状态
 */
void
hdi_init(struct gw_hdma_chan *c0, struct gw_hdma_chan *c1,
         uint32_t fw_version)
{
    ch0 = c0;
    ch1 = c1;
    fw_ver = fw_version;
    drop_all();
    ctrl = CTRL_RESET;
    csts = 0;
    busy = 0;
    ctx_err = 0;
    int_level = gw_read32(GW_INT_LEVEL);
}

/**
 * FLR 之后：HIF 中 HDI 的状态和 HDMA 已随 FLR 复位，固件丢弃自己的
 * 全部 HDI 状态和信箱中旧的 doorbell
 */
void
hdi_reset(void)
{
    drop_all();
    ctrl = CTRL_RESET;
    csts = 0;
    busy = 0;
    ctx_err = 0;
    int_level = 0;  /* FLR 清零 INT_LEVEL */
    gw_write32(GW_DB_PENDING0, ~0u);
    gw_write32(GW_DB_PENDING1, ~0u);
    gw_write32(GW_EVT_PENDING, GW_EVT_HDI_WR);
}

/**
 * 处理事件 ev 中属于 HDI 的位（HDI_WR、HDMA_NOTIFY、HDMA_ERR），返回
 * 由本模块按协议清零的位；主循环不得再清零它们
 */
uint32_t
hdi_event(uint32_t ev)
{
    if (ev & GW_EVT_HDI_WR)
        on_hdi_wr();
    if (ev & GW_EVT_HDMA_ERR) {
        /* 先清零再检查全部通道（spec-host-dma §8.2） */
        gw_write32(GW_EVT_PENDING, GW_EVT_HDMA_ERR);
        for (unsigned i = 0; i < 2; i++) {
            uint32_t st = gw_read32(GW_HDMA_CH(i, GW_CH_STATUS)) & 3;
            if (st == GW_CH_ERRSTOP)
                ch_recover(i);
        }
    }
    /* HDMA_NOTIFY 由 hdi_poll 在读 CH_HEAD 之前清零 */
    return GW_EVT_HDI_WR | GW_EVT_HDMA_NOTIFY | GW_EVT_HDMA_ERR;
}

/** 处理 doorbell 信箱：先清零 DB_PENDING，再读取并检查被写过的槽位 */
void
hdi_doorbells(void)
{
    uint64_t p = gw_read32(GW_DB_PENDING0) |
                 (uint64_t)gw_read32(GW_DB_PENDING1) << 32;
    if (!p)
        return;
    gw_write32(GW_DB_PENDING0, (uint32_t)p);
    gw_write32(GW_DB_PENDING1, (uint32_t)(p >> 32));
    if (ctrl != CTRL_RUN && ctrl != CTRL_SHUT)
        return;
    for (unsigned i = 0; i < 2 * NQ; i++) {
        if ((p >> i) & 1)
            doorbell(i, gw_read32(GW_DB_SLOT(i)) & 0xFFFF);
    }
}

/**
 * 推进命令：回收数据通道的完成、写出完成记录、检查等待与计时、开始
 * 新命令，并按计时设置唤醒期限。返回非 0 表示本次的工作量用完时还有
 * 可以立即推进的工作，主循环不应睡眠
 */
int
hdi_poll(void)
{
    if (ctrl == CTRL_SHUT_NOW)
        shut_now();
    budget = POLL_BUDGET;
    int progress = 1;
    while (progress && budget > 0 &&
           (ctrl == CTRL_RUN || ctrl == CTRL_SHUT)) {
        /* 先清零 HDMA_NOTIFY 再读 CH_HEAD（spec-host-dma §5.3） */
        gw_write32(GW_EVT_PENDING, GW_EVT_HDMA_NOTIFY);
        progress = reap1() != 0;
        for (unsigned i = 0; i < NQ; i++)
            progress |= step(i);
        progress |= submit();
        if (ctrl == CTRL_RUN)
            progress |= start_cmds() != 0;
        else if (ctrl == CTRL_SHUT)
            shut_check();
    }
    timers();
    return budget <= 0;
}
