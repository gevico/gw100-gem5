/**
 * @file  test.h
 * @brief 裸机测试的入口约定、断言与退出码
 *
 * 每个 test 定义 `int test_main(void)`，返回 0 表示通过、非 0 表示失败。
 * 断言失败时打印位置和实际值，立即以 BM_EXIT_FAIL 结束仿真。
 * 退出码同时是 gem5 的进程退出码，测试运行器据此判定结果。
 */

#ifndef BM_TEST_H
#define BM_TEST_H

#include "bm/console.h"

enum {
    BM_EXIT_PASS = 0,   /* 通过 */
    BM_EXIT_FAIL = 1,   /* 断言失败，或 test_main 返回非 0 */
    BM_EXIT_CRASH = 2,  /* 发生未预期的异常 */
};

/* 每个 test 实现这个函数 */
int test_main(void);

/* 打印失败信息并以 BM_EXIT_FAIL 结束；不返回 */
__attribute__((noreturn, format(printf, 3, 4)))
void bm_test_fail(const char *file, int line, const char *fmt, ...);

/* 打印一条带 [INFO] 前缀的日志 */
#define TEST_INFO(...) bm_printf("[INFO] " __VA_ARGS__)

#define TEST_FAIL(...) bm_test_fail(__FILE__, __LINE__, __VA_ARGS__)

#define TEST_ASSERT(cond)                                                \
    do {                                                                 \
        if (!(cond))                                                     \
            TEST_FAIL("断言失败：%s", #cond);                             \
    } while (0)

#define TEST_EQ(a, b)                                                    \
    do {                                                                 \
        unsigned long long _a = (unsigned long long)(a);                 \
        unsigned long long _b = (unsigned long long)(b);                 \
        if (_a != _b)                                                    \
            TEST_FAIL("%s == %s 不成立：0x%llx != 0x%llx", #a, #b, _a, _b); \
    } while (0)

#define TEST_NE(a, b)                                                    \
    do {                                                                 \
        unsigned long long _a = (unsigned long long)(a);                 \
        unsigned long long _b = (unsigned long long)(b);                 \
        if (_a == _b)                                                    \
            TEST_FAIL("%s != %s 不成立：都是 0x%llx", #a, #b, _a);         \
    } while (0)

#endif /* BM_TEST_H */
