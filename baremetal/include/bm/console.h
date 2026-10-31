/**
 * @file  console.h
 * @brief 格式化输出（printf 子集）
 *
 * 支持的转换：%c %s %d %i %u %x %X %p %%；长度修饰 l、ll、z；
 * 标志 '0'、'-'；宽度。不支持浮点和精度。
 */

#ifndef BM_CONSOLE_H
#define BM_CONSOLE_H

#include <stdarg.h>
#include <stddef.h>

/** 按 fmt 格式化后经 semihosting 输出到控制台，返回输出的字符数 */
int bm_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** 同 bm_printf，参数以 va_list 传入 */
int bm_vprintf(const char *fmt, va_list ap);

/**
 * 按 fmt 格式化到 buf，最多写 size − 1 个字符并补 0；返回完整结果应有的
 * 长度（不含结尾的 0），与 C 标准的 snprintf 一致
 */
int bm_snprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/** 同 bm_snprintf，参数以 va_list 传入 */
int bm_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

#endif /* BM_CONSOLE_H */
