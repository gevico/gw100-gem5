/**
 * @file  string.h
 * @brief 公共库提供的字符串与内存函数
 *
 * 即使用了 -ffreestanding，GCC 仍可能生成对 memcpy、memset 等的调用，
 * 所以公共库必须提供它们。全部按字节操作：MMU 关闭时内存按 Device 类型
 * 访问，不允许不对齐的宽访问。
 */

#ifndef BM_STRING_H
#define BM_STRING_H

#include <stddef.h>

/** 把 src 起的 n 个字节复制到 dst；两者不得重叠。返回 dst */
void *memcpy(void *dst, const void *src, size_t n);

/** 把 src 起的 n 个字节复制到 dst；两者可以重叠。返回 dst */
void *memmove(void *dst, const void *src, size_t n);

/** 把 dst 起的 n 个字节都置为 (unsigned char)c。返回 dst */
void *memset(void *dst, int c, size_t n);

/**
 * 逐字节比较 a、b 的前 n 个字节；相等返回 0，否则按第一个不同字节的
 * 大小返回 -1 或 1
 */
int memcmp(const void *a, const void *b, size_t n);

/** 以 0 结尾的字符串 s 的长度，不含结尾的 0 */
size_t strlen(const char *s);

/** 比较两个以 0 结尾的字符串；相等返回 0，否则返回第一个不同字符之差 */
int strcmp(const char *a, const char *b);

#endif /* BM_STRING_H */
