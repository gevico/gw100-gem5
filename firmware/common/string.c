/**
 * @file  string.c
 * @brief memcpy、memset：编译器在拷贝或清零结构体、数组时会调用它们
 *
 * 启动 ROM 和固件不链接标准库，这里提供编译器隐式需要的最小实现。
 */

#include <stddef.h>
#include <stdint.h>

/** 把 src 起的 n 字节拷贝到 dst，返回 dst */
void *
memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

/** 把 dst 起的 n 字节置为 c，返回 dst */
void *
memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}
