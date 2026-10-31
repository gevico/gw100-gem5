/**
 * @file  semihost.c
 * @brief Arm semihosting 调用的实现
 */

#include "bm/semihost.h"

/** semihosting 操作号：输出字符串 */
#define SYS_WRITE0                      0x04
/** semihosting 操作号：结束程序 */
#define SYS_EXIT                        0x18
/** SYS_EXIT 的原因：应用程序正常退出 */
#define ADP_STOPPED_APPLICATION_EXIT    0x20026

/** 发起一次 semihosting 调用：x0 为操作号，x1 指向参数；返回 x0 */
static long
semihost_call(unsigned long op, const void *arg)
{
    register unsigned long x0 __asm__("x0") = op;
    register const void *x1 __asm__("x1") = arg;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    return (long)x0;
}

/** 经 semihosting 输出一个以 0 结尾的字符串 */
void
bm_semihost_write0(const char *s)
{
    semihost_call(SYS_WRITE0, s);
}

/** 以 ADP_Stopped_ApplicationExit 结束仿真，code 为退出码；不返回 */
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
