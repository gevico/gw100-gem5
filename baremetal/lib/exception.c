/**
 * @file  exception.c
 * @brief 未预期异常的报告
 *
 * 打印异常类型和 ESR、ELR、FAR，以 BM_EXIT_CRASH 结束仿真。
 */

#include <stdint.h>

#include "bm/console.h"
#include "bm/semihost.h"
#include "bm/test.h"

/** 读系统寄存器 name 的值 */
#define READ_SYSREG(name)                                                \
    ({                                                                   \
        uint64_t _v;                                                     \
        __asm__ volatile("mrs %0, " #name : "=r"(_v));                   \
        _v;                                                              \
    })

/** 16 个异常向量入口的名字，下标即向量编号 */
static const char *const vector_names[16] = {
    "当前 EL/SP0 同步", "当前 EL/SP0 IRQ",
    "当前 EL/SP0 FIQ", "当前 EL/SP0 SError",
    "当前 EL/SPx 同步", "当前 EL/SPx IRQ",
    "当前 EL/SPx FIQ", "当前 EL/SPx SError",
    "低 EL/A64 同步", "低 EL/A64 IRQ", "低 EL/A64 FIQ", "低 EL/A64 SError",
    "低 EL/A32 同步", "低 EL/A32 IRQ", "低 EL/A32 FIQ", "低 EL/A32 SError",
};

/**
 * vectors.S 的全部入口都跳到这里：idx 为向量编号。打印异常类型和
 * ESR、ELR、FAR，以 BM_EXIT_CRASH 结束仿真；不返回
 */
__attribute__((noreturn)) void
bm_exception(unsigned long idx)
{
    unsigned el = (unsigned)(READ_SYSREG(CurrentEL) >> 2) & 3;
    uint64_t esr, elr, far;

    if (el == 3) {
        esr = READ_SYSREG(esr_el3);
        elr = READ_SYSREG(elr_el3);
        far = READ_SYSREG(far_el3);
    } else if (el == 2) {
        esr = READ_SYSREG(esr_el2);
        elr = READ_SYSREG(elr_el2);
        far = READ_SYSREG(far_el2);
    } else {
        esr = READ_SYSREG(esr_el1);
        elr = READ_SYSREG(elr_el1);
        far = READ_SYSREG(far_el1);
    }

    bm_printf("[CRASH] 异常：%s（向量 %lu，EL%u）\n",
              vector_names[idx & 15], idx, el);
    bm_printf("[CRASH] ESR=0x%016llx（EC=0x%02llx）\n",
              (unsigned long long)esr,
              (unsigned long long)((esr >> 26) & 0x3f));
    bm_printf("[CRASH] ELR=0x%016llx FAR=0x%016llx\n",
              (unsigned long long)elr, (unsigned long long)far);
    bm_semihost_exit(BM_EXIT_CRASH);
}
