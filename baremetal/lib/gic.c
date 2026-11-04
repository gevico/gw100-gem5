/**
 * @file  gic.c
 * @brief GICv3 与 ITS 最小驱动的实现
 *
 * @see spec-pcie
 */

#include "bm/gic.h"

#include "bm/mmio.h"

/** GICD_CTLR */
#define GICD_CTLR               0x0000
/** GICD_CTLR.EnableGrp1NS（DS = 1 时即 Group 1 的使能） */
#define GICD_CTLR_GRP1NS        (1u << 1)
/** GICD_CTLR.ARE_S */
#define GICD_CTLR_ARE_S         (1u << 4)
/** GICD_CTLR.ARE_NS */
#define GICD_CTLR_ARE_NS        (1u << 5)
/** GICD_CTLR.DS：关闭安全扩展 */
#define GICD_CTLR_DS            (1u << 6)

/** GICR_CTLR（本核 redistributor 的 RD_base 帧） */
#define GICR_CTLR               0x0000
/** GICR_CTLR.EnableLPIs */
#define GICR_CTLR_LPIS          (1u << 0)
/** GICR_WAKER */
#define GICR_WAKER              0x0014
/** GICR_WAKER.ProcessorSleep */
#define GICR_WAKER_SLEEP        (1u << 1)
/** GICR_WAKER.ChildrenAsleep */
#define GICR_WAKER_ASLEEP       (1u << 2)
/** GICR_PROPBASER */
#define GICR_PROPBASER          0x0070
/** GICR_PENDBASER */
#define GICR_PENDBASER          0x0078

/** GITS_CTLR */
#define GITS_CTLR               0x0000
/** GITS_CTLR.Enabled */
#define GITS_CTLR_EN            (1u << 0)
/** GITS_CBASER */
#define GITS_CBASER             0x0080
/** GITS_CWRITER */
#define GITS_CWRITER            0x0088
/** GITS_CREADR */
#define GITS_CREADR             0x0090
/** GITS_BASER<n> */
#define GITS_BASER(n)           (0x0100 + 8 * (n))
/** GITS_BASER、GITS_CBASER 的 Valid 位 */
#define GITS_VALID              (1ull << 63)

/** LPI 配置表与 pending 表按 14 位 ID 分配：GICR_PROPBASER.IDbits */
#define LPI_ID_BITS             13u
/** pending 表：64 KiB 对齐，按 BM_LPI_END 个 ID 每位一个 */
#define MEM_PEND                (BM_GIC_MEM_BASE + 0x00000)
/** LPI 配置表：每个 LPI 一个字节 */
#define MEM_PROP                (BM_GIC_MEM_BASE + 0x10000)
/** ITS 的设备表：一页 4 KiB */
#define MEM_DEVTAB              (BM_GIC_MEM_BASE + 0x12000)
/** ITS 的集合表：一页 4 KiB */
#define MEM_COLLTAB             (BM_GIC_MEM_BASE + 0x13000)
/** ITS 的命令队列：一页 4 KiB，128 条命令 */
#define MEM_CMDQ                (BM_GIC_MEM_BASE + 0x14000)
/** 命令队列的字节数 */
#define CMDQ_BYTES              0x1000u
/** ITT：每个设备 4 KiB，从这里起依次分配 */
#define MEM_ITT                 (BM_GIC_MEM_BASE + 0x15000)
/** 最多映射的设备数 */
#define MAX_DEVICES             4u
/** 每个设备的 ITT 字节数 */
#define ITT_BYTES               0x1000u
/** LPI 的优先级（配置表的 [7:2]） */
#define LPI_PRIO                0xA0u
/** 等待 ITS 执行命令的轮询次数上限 */
#define CMD_POLLS               1000000u

/** ITS 命令：SYNC */
#define CMD_SYNC                0x05u
/** ITS 命令：MAPD */
#define CMD_MAPD                0x08u
/** ITS 命令：MAPC */
#define CMD_MAPC                0x09u
/** ITS 命令：MAPTI */
#define CMD_MAPTI               0x0Au
/** ITS 命令：INV */
#define CMD_INV                 0x0Cu

/** 注册的中断处理程序 */
static bm_irq_handler_t irq_handler;
/** 命令队列的写指针（字节偏移） */
static uint32_t cmdq_wr;
/** 已经 MAPD 的设备 */
static uint32_t mapped_dev[MAX_DEVICES];
/** 已经 MAPD 的设备数 */
static unsigned mapped_count;

/** 清零 [at, at + n)，n 为 8 的倍数 */
static void
zero(uintptr_t at, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 8)
        *(volatile uint64_t *)(at + i) = 0;
}

/**
 * 初始化 GICv3 并打开 IRQ：distributor 置 DS、ARE 并使能 Group 1，唤醒
 * 本核的 redistributor，设置 CPU 接口（SRE、PMR、Group 1 使能），
 * SCR_EL3.IRQ = 1，清除 PSTATE.I。handler 处理之后到达的每个中断
 */
void
bm_gic_init(bm_irq_handler_t handler)
{
    irq_handler = handler;
    bm_write32(BM_GICD_BASE + GICD_CTLR,
               GICD_CTLR_DS | GICD_CTLR_ARE_NS | GICD_CTLR_ARE_S |
               GICD_CTLR_GRP1NS);

    uintptr_t rd = BM_GICR_BASE;
    bm_write32(rd + GICR_WAKER,
               bm_read32(rd + GICR_WAKER) & ~GICR_WAKER_SLEEP);
    while (bm_read32(rd + GICR_WAKER) & GICR_WAKER_ASLEEP)
        ;

    /* ICC_SRE_EL3：SRE、DFB、DIB、Enable；然后 PMR 放行全部优先级，
     * 使能 Group 1（EL3 的 ICC_IGRPEN1_EL3 同时控制两个安全状态） */
    __asm__ volatile("msr icc_sre_el3, %0\n\tisb" ::"r"(0xFull));
    __asm__ volatile("msr icc_pmr_el1, %0" ::"r"(0xFFull));
    __asm__ volatile("msr icc_bpr1_el1, %0" ::"r"(0ull));
    __asm__ volatile("msr icc_igrpen1_el3, %0\n\tisb" ::"r"(3ull));

    /* IRQ 送到 EL3，再打开 PSTATE.I */
    uint64_t scr;
    __asm__ volatile("mrs %0, scr_el3" : "=r"(scr));
    __asm__ volatile("msr scr_el3, %0\n\tisb" ::"r"(scr | (1ull << 1)));
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

/** 把一条 4 个双字的命令写入命令队列并提交；等待 ITS 执行完 */
static int
its_cmd(uint64_t d0, uint64_t d1, uint64_t d2, uint64_t d3)
{
    volatile uint64_t *c = (volatile uint64_t *)(MEM_CMDQ + cmdq_wr);
    c[0] = d0;
    c[1] = d1;
    c[2] = d2;
    c[3] = d3;
    cmdq_wr = (cmdq_wr + 32) % CMDQ_BYTES;
    bm_dsb();
    bm_write64(BM_GITS_BASE + GITS_CWRITER, cmdq_wr);
    for (uint32_t i = 0; i < CMD_POLLS; i++) {
        uint64_t rd = bm_read64(BM_GITS_BASE + GITS_CREADR);
        if (rd & 1)
            return -1;  /* Stalled：命令出错 */
        if ((rd & ~0x1Full) == cmdq_wr)
            return 0;
    }
    return -1;
}

/**
 * 初始化 LPI 与 ITS：建立 LPI 配置表和 pending 表并使能本核
 * redistributor 的 LPI，设置 ITS 的设备表、集合表和命令队列，使能 ITS，
 * 再用 MAPC 把集合 0 映射到本核。须在 bm_gic_init 之后调用。成功返回 0
 */
int
bm_its_init(void)
{
    zero(MEM_PEND, BM_LPI_END / 8);
    zero(MEM_PROP, BM_LPI_END - BM_LPI_BASE);
    zero(MEM_DEVTAB, 0x1000);
    zero(MEM_COLLTAB, 0x1000);
    zero(MEM_CMDQ, CMDQ_BYTES);
    bm_dsb();
    mapped_count = 0;
    cmdq_wr = 0;

    uintptr_t rd = BM_GICR_BASE;
    bm_write64(rd + GICR_PROPBASER, MEM_PROP | LPI_ID_BITS);
    bm_write64(rd + GICR_PENDBASER, MEM_PEND);
    bm_write32(rd + GICR_CTLR, bm_read32(rd + GICR_CTLR) | GICR_CTLR_LPIS);

    /* 设备表（BASER0）与集合表（BASER1）各一页 4 KiB：PageSize = 0、
     * Size = 0；Type、Entry_Size 为只读 */
    uintptr_t its = BM_GITS_BASE;
    bm_write32(its + GITS_CTLR, 0);
    bm_write64(its + GITS_BASER(0), GITS_VALID | MEM_DEVTAB);
    bm_write64(its + GITS_BASER(1), GITS_VALID | MEM_COLLTAB);
    bm_write64(its + GITS_CBASER, GITS_VALID | MEM_CMDQ);
    bm_write64(its + GITS_CWRITER, 0);
    bm_write32(its + GITS_CTLR, GITS_CTLR_EN);

    /* GITS_TYPER.PTA = 0：RDbase 是处理器号，本核为 0 */
    if (its_cmd(CMD_MAPC, 0, GITS_VALID | (0ull << 16) | 0, 0) != 0)
        return -1;
    return its_cmd(CMD_SYNC, 0, 0ull << 16, 0);
}

/**
 * 把设备 device_id 的事件 event_id 映射到 LPI lpi，并使能该 LPI：
 * 设备第一次映射时发 MAPD，然后发 MAPTI、INV、SYNC 并等待 ITS 执行完。
 * 成功返回 0；参数越界、ITS 报错或超时返回 -1
 */
int
bm_its_map(uint32_t device_id, uint32_t event_id, uint32_t lpi)
{
    if (device_id >= BM_ITS_MAX_DEVICE_ID ||
        event_id >= BM_ITS_MAX_EVENTS || lpi < BM_LPI_BASE ||
        lpi >= BM_LPI_END)
        return -1;
    unsigned k = 0;
    while (k < mapped_count && mapped_dev[k] != device_id)
        k++;
    if (k == mapped_count) {
        if (mapped_count == MAX_DEVICES)
            return -1;
        uintptr_t itt = MEM_ITT + ITT_BYTES * k;
        zero(itt, ITT_BYTES);
        bm_dsb();
        /* Size = EventID 位数 − 1：32 个事件为 4 */
        if (its_cmd(CMD_MAPD | (uint64_t)device_id << 32, 4,
                    GITS_VALID | itt, 0) != 0)
            return -1;
        mapped_dev[mapped_count++] = device_id;
    }

    /* 先写 LPI 配置（使能、优先级），再建立映射、使配置生效 */
    *(volatile uint8_t *)(MEM_PROP + (lpi - BM_LPI_BASE)) =
        (uint8_t)(LPI_PRIO | 1u);
    bm_dsb();
    if (its_cmd(CMD_MAPTI | (uint64_t)device_id << 32,
                event_id | (uint64_t)lpi << 32, 0, 0) != 0)
        return -1;
    if (its_cmd(CMD_INV | (uint64_t)device_id << 32, event_id, 0, 0) != 0)
        return -1;
    return its_cmd(CMD_SYNC, 0, 0, 0);
}

/** vectors.S 的 IRQ 入口调用：应答并处理全部待处理的 Group 1 中断 */
void
bm_irq_dispatch(void)
{
    for (;;) {
        uint64_t iar;
        __asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(iar));
        uint32_t intid = (uint32_t)iar & 0xFFFFFF;
        if (intid >= 1020 && intid <= 1023)
            return;  /* 没有待处理的中断 */
        if (irq_handler)
            irq_handler(intid);
        __asm__ volatile("msr icc_eoir1_el1, %0\n\tisb" ::"r"(iar));
    }
}
