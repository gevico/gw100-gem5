/**
 * @file  lib_selftest.c
 * @brief 公共库自检：字符串函数、格式化输出、断言宏
 *
 * 其他测试都依赖这些函数报告结果，所以先单独验证它们本身是对的。
 */

#include <stddef.h>
#include <stdint.h>

#include "bm/console.h"
#include "bm/string.h"
#include "bm/test.h"

/** 格式化结果 got 必须等于 expect，返回值 ret 必须等于 expect 的长度 */
static void
check_fmt(const char *expect, const char *got, int ret)
{
    if (strcmp(expect, got) != 0)
        TEST_FAIL("格式化结果 \"%s\"，期望 \"%s\"", got, expect);
    TEST_EQ(ret, strlen(expect));
}

/** 用 bm_snprintf 按后面的参数格式化，结果必须等于 expect */
#define FMT(expect, ...)                                                 \
    do {                                                                 \
        char _b[64];                                                     \
        int _r = bm_snprintf(_b, sizeof(_b), __VA_ARGS__);               \
        check_fmt(expect, _b, _r);                                       \
    } while (0)

/** 字符串与内存函数：memset、memcpy、memcmp、重叠的 memmove、strlen、strcmp */
static void
test_string(void)
{
    char a[16], b[16];

    memset(a, 'x', sizeof(a));
    for (size_t i = 0; i < sizeof(a); i++)
        TEST_EQ(a[i], 'x');

    memcpy(b, "0123456789", 11);
    TEST_EQ(strlen(b), 10);
    TEST_EQ(memcmp(b, "0123456789", 11), 0);
    TEST_ASSERT(memcmp(b, "0123456780", 10) > 0);

    /* 重叠拷贝：向后和向前各一次 */
    memmove(b + 2, b, 5);
    TEST_EQ(memcmp(b, "0101234789", 10), 0);
    memmove(b, b + 2, 5);
    TEST_EQ(memcmp(b, "0123434789", 10), 0);

    TEST_EQ(strcmp("abc", "abc"), 0);
    TEST_ASSERT(strcmp("abc", "abd") < 0);
    TEST_ASSERT(strcmp("abcd", "abc") > 0);
}

/** 格式化：各种转换、宽度、补零、左对齐、长度修饰、空指针和截断 */
static void
test_format(void)
{
    FMT("42", "%d", 42);
    FMT("-42", "%d", -42);
    FMT("4294967295", "%u", 0xFFFFFFFFu);
    FMT("ff", "%x", 0xFFu);
    FMT("FF", "%X", 0xFFu);
    FMT("000000ff", "%08x", 0xFFu);
    FMT("-0042", "%05d", -42);
    FMT("  7", "%3d", 7);
    FMT("ab  |", "%-4s|", "ab");
    FMT("ffffffffffffffff", "%llx", 0xFFFFFFFFFFFFFFFFull);
    FMT("-9223372036854775808", "%lld",
        (long long)(-9223372036854775807LL - 1));
    FMT("123456789abc", "%lx", 0x123456789abcUL);
    FMT("0x80000000", "%p", (void *)0x80000000UL);
    FMT("c%", "%c%%", 'c');
    /* 用 volatile 变量传入空指针，避免编译期的格式检查报错 */
    const char *volatile null_str = 0;
    FMT("(null)", "%s", null_str);

    /* 截断：返回完整长度，缓冲区以 0 结尾 */
    char small[4];
    int r = bm_snprintf(small, sizeof(small), "%s", "abcdef");
    TEST_EQ(r, 6);
    TEST_EQ(strcmp(small, "abc"), 0);
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    TEST_EQ(1 + 1, 2);
    TEST_NE(1, 2);
    test_string();
    test_format();
    TEST_INFO("字符串函数、格式化输出、断言宏均正常\n");
    return 0;
}
