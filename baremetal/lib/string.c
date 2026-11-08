/**
 * @file  string.c
 * @brief 公共库字符串与内存函数的实现
 */

#include "bm/string.h"

/** 把 src 起的 n 个字节复制到 dst；两者不得重叠。返回 dst */
void *
memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

/** 把 src 起的 n 个字节复制到 dst；两者可以重叠。返回 dst */
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

/** 把 dst 起的 n 个字节都置为 (unsigned char)c。返回 dst */
void *
memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

/**
 * 逐字节比较 a、b 的前 n 个字节；相等返回 0，否则按第一个不同字节的
 * 大小返回 -1 或 1
 */
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

/** 以 0 结尾的字符串 s 的长度，不含结尾的 0 */
size_t
strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

/** 比较两个以 0 结尾的字符串；相等返回 0，否则返回第一个不同字符之差 */
int
strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
