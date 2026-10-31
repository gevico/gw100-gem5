/**
 * @file  console.c
 * @brief printf 子集的实现
 *
 * 格式化核心 vformat 通过回调输出字符：bm_vsnprintf 写进缓冲区，
 * bm_vprintf 攒满一小段后经 semihosting 输出。
 */

#include <stdint.h>

#include "bm/console.h"
#include "bm/semihost.h"

/** 输出一个字符的回调；ctx 是调用方的上下文 */
typedef void (*putc_fn)(char c, void *ctx);

/**
 * 按宽度 width 输出长度为 len 的 s：left 为 1 时左对齐，否则用 pad 补在
 * 左边；补零时负号放在零的前面。输出的字符数累加到 *count
 */
static void
emit_padded(putc_fn put, void *ctx, const char *s, int len, int width,
            int left, char pad, int *count)
{
    int fill = width > len ? width - len : 0;

    if (!left && pad == '0' && len > 0 && s[0] == '-') {
        /* 负数补零时，符号放在零前面 */
        put('-', ctx);
        (*count)++;
        s++;
        len--;
    }
    if (!left) {
        for (int i = 0; i < fill; i++) {
            put(pad, ctx);
            (*count)++;
        }
    }
    for (int i = 0; i < len; i++) {
        put(s[i], ctx);
        (*count)++;
    }
    if (left) {
        for (int i = 0; i < fill; i++) {
            put(' ', ctx);
            (*count)++;
        }
    }
}

/** 把 v 按 base 进制写成数字串放进 buf（不补 0 结尾），返回位数 */
static int
format_uint(char *buf, unsigned long long v, unsigned base, int upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    int n = 0;

    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    return n;
}

/** 格式化核心：按 fmt 解析参数，经 put 逐个输出字符，返回输出的字符数 */
static int
vformat(putc_fn put, void *ctx, const char *fmt, va_list ap)
{
    int count = 0;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(*fmt, ctx);
            count++;
            continue;
        }
        fmt++;

        int left = 0;
        char pad = ' ';
        for (;; fmt++) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                pad = '0';
            else
                break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        int lng = 0;
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }
        if (*fmt == 'z') {
            lng = 1;
            fmt++;
        }
        if (left)
            pad = ' ';

        char buf[24];
        int len = 0;
        unsigned long long u;
        long long s;

        switch (*fmt) {
          case 'c':
            buf[0] = (char)va_arg(ap, int);
            emit_padded(put, ctx, buf, 1, width, left, ' ', &count);
            break;
          case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str)
                str = "(null)";
            int slen = 0;
            while (str[slen])
                slen++;
            emit_padded(put, ctx, str, slen, width, left, ' ', &count);
            break;
          }
          case 'd':
          case 'i':
            s = lng >= 2 ? va_arg(ap, long long)
                : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
            if (s < 0) {
                buf[0] = '-';
                len = 1 + format_uint(buf + 1, 0ULL - (unsigned long long)s,
                                      10, 0);
            } else {
                len = format_uint(buf, (unsigned long long)s, 10, 0);
            }
            emit_padded(put, ctx, buf, len, width, left, pad, &count);
            break;
          case 'u':
          case 'x':
          case 'X':
            u = lng >= 2 ? va_arg(ap, unsigned long long)
                : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            len = format_uint(buf, u, *fmt == 'u' ? 10 : 16, *fmt == 'X');
            emit_padded(put, ctx, buf, len, width, left, pad, &count);
            break;
          case 'p':
            buf[0] = '0';
            buf[1] = 'x';
            u = (uintptr_t)va_arg(ap, void *);
            len = 2 + format_uint(buf + 2, u, 16, 0);
            emit_padded(put, ctx, buf, len, width, left, ' ', &count);
            break;
          case '%':
            put('%', ctx);
            count++;
            break;
          case '\0':
            return count;
          default:
            /* 不支持的转换原样输出，便于发现 */
            put('%', ctx);
            put(*fmt, ctx);
            count += 2;
            break;
        }
    }
    return count;
}

/** bm_vsnprintf 的输出上下文 */
struct buf_ctx {
    /** 目标缓冲区 */
    char *buf;
    /** 缓冲区大小 */
    size_t size;
    /** 已产生的字符数，可能超过 size */
    size_t pos;
};

/** 把字符写进缓冲区；超出容量的部分只计数不写入 */
static void
buf_put(char c, void *p)
{
    struct buf_ctx *b = p;
    if (b->pos + 1 < b->size)
        b->buf[b->pos] = c;
    b->pos++;
}

/** 同 bm_snprintf，参数以 va_list 传入 */
int
bm_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct buf_ctx b = { buf, size, 0 };
    int n = vformat(buf_put, &b, fmt, ap);
    if (size > 0)
        buf[b.pos < size ? b.pos : size - 1] = '\0';
    return n;
}

/**
 * 按 fmt 格式化到 buf，最多写 size − 1 个字符并补 0；返回完整结果应有的
 * 长度（不含结尾的 0），与 C 标准的 snprintf 一致
 */
int
bm_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = bm_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/** bm_vprintf 的输出上下文：攒满一段再经 semihosting 输出 */
struct con_ctx {
    /** 待输出的字符，末尾留一个位置给结尾的 0 */
    char buf[128];
    /** buf 中已有的字符数 */
    size_t pos;
};

/** 把攒下的字符经 semihosting 输出并清空 */
static void
con_flush(struct con_ctx *c)
{
    c->buf[c->pos] = '\0';
    bm_semihost_write0(c->buf);
    c->pos = 0;
}

/** 攒一个字符，攒满时输出 */
static void
con_put(char c, void *p)
{
    struct con_ctx *con = p;
    con->buf[con->pos++] = c;
    if (con->pos == sizeof(con->buf) - 1)
        con_flush(con);
}

/** 同 bm_printf，参数以 va_list 传入 */
int
bm_vprintf(const char *fmt, va_list ap)
{
    struct con_ctx con;
    con.pos = 0;
    int n = vformat(con_put, &con, fmt, ap);
    if (con.pos)
        con_flush(&con);
    return n;
}

/** 按 fmt 格式化后经 semihosting 输出到控制台，返回输出的字符数 */
int
bm_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = bm_vprintf(fmt, ap);
    va_end(ap);
    return n;
}
