/**
 * @file  semihost.c
 * @brief Arm semihosting 调用的实现
 */

#include "bm/semihost.h"

#define SYS_WRITE0                  0x04
#define SYS_EXIT                    0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026

static long
semihost_call(unsigned long op, const void *arg)
{
    register unsigned long x0 __asm__("x0") = op;
    register const void *x1 __asm__("x1") = arg;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    return (long)x0;
}

void
bm_semihost_write0(const char *s)
{
    semihost_call(SYS_WRITE0, s);
}

void
bm_semihost_exit(unsigned long code)
{
    /* AArch64 的 SYS_EXIT 参数块：{原因, 子码} */
    static unsigned long block[2];
    block[0] = ADP_STOPPED_APPLICATION_EXIT;
    block[1] = code;
    semihost_call(SYS_EXIT, block);
    for (;;)
        __asm__ volatile("wfe");
}
