/**
 * @file  string.c
 * @brief 编译器和测试需要的最小字符串函数
 *
 * 即使用了 -ffreestanding，GCC 仍可能生成对 memcpy、memset 等的调用，
 * 所以必须提供。全部按字节操作：MMU 关闭时内存按 Device 类型访问，
 * 不允许不对齐的宽访问。
 */

#include <stddef.h>

void *
memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *
memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

void *
memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

int
memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) {
        if (*x != *y)
            return *x < *y ? -1 : 1;
    }
    return 0;
}

size_t
strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int
strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
