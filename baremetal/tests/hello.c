/**
 * @file  hello.c
 * @brief 冒烟测试：启动、控制台输出和退出码是否正常
 */

#include <stdint.h>

#include "bm/test.h"

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    TEST_INFO("hello from GW100 baremetal, EL%u\n", (unsigned)((el >> 2) & 3));
    return 0;
}
