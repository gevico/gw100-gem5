/**
 * @file  main.c
 * @brief 多 ISA 可行性测试的 RV32 程序：整数运算、栈和独立存储的自检
 *
 * 可行性测试，不是产品程序。每个 RV32 核运行以 CORE_ID 编译的一份实例，
 * 只使用自己的存储：
 * - 整数运算：M 扩展的乘法、高位乘法、有符号与无符号的除法和取余，
 *   逐项与只用基本指令的移位相加乘法、恢复除法比较；
 * - 栈：递归计算 fib(20)，并在栈上的数组中填写、求和；
 * - 独立存储：先在 BSS 中写入以 CORE_ID 区分的花样，其余检查结束后再
 *   读回比较；已初始化数据须是镜像中的值。
 * 结果经 semihosting 输出一行，再以 SYS_EXIT_EXTENDED 退出，子码为
 * 0x100 | 失败位 0x80 | CORE_ID；返回后由 crt0.S 在 wfi 中停住。
 *
 * @see gem5-ext/configs/feasibility/README.md
 */

#include <stdint.h>

#ifndef CORE_ID
#error "以 -DCORE_ID=<0..127> 编译"
#endif

/** semihosting：输出以 0 结尾的字符串 */
#define SYS_WRITE0          0x04u
/** semihosting：带子码的退出 */
#define SYS_EXIT_EXTENDED   0x20u
/** semihosting 的退出原因 ADP_Stopped_ApplicationExit */
#define ADP_APP_EXIT        0x20026u
/** 退出子码的基值，低 7 位为 CORE_ID */
#define EXIT_BASE           0x100u
/** 退出子码的失败位 */
#define EXIT_FAIL           0x80u
/** 整数运算检查的轮数 */
#define INT_ROUNDS          256u
/** 栈上数组的字数 */
#define STACK_WORDS         256u
/** 独立存储花样的字数 */
#define STORE_WORDS         1024u
/** 递归计算的 fib 参数与期望值 */
#define FIB_N               20u
/** fib(FIB_N) */
#define FIB_EXPECT          6765u

/** 经 semihosting 调用 op，参数为 arg（crt0.S） */
uint32_t semihost(uint32_t op, const void *arg);
/** 异常入口调用：报告 mcause、mepc 后以失败退出（crt0.S） */
void on_trap(uint32_t mcause, uint32_t mepc);
/** 程序入口（crt0.S 调用） */
int main(void);

/** 已初始化数据：由镜像装入，检查读到的是镜像中的值 */
static volatile uint32_t data_magic = 0x5EED0000u | CORE_ID;
/** 整数运算的种子；volatile 使编译器不能在编译时算出结果 */
static volatile uint32_t seed = 0x9E3779B9u ^ (CORE_ID * 0x01000193u);
/** 递归的参数；volatile 使递归不能在编译时展开 */
static volatile uint32_t fib_n = FIB_N;
/** 独立存储：以 CORE_ID 区分的花样 */
static uint32_t store[STORE_WORDS];
/** 输出缓冲 */
static char line[192];
/** line 的写位置 */
static unsigned pos;

/** 追加字符串 s */
static void
put_s(const char *s)
{
    while (*s && pos < sizeof(line) - 1)
        line[pos++] = *s++;
}

/** 追加 v 的十进制表示 */
static void
put_u(uint32_t v)
{
    char d[10];
    unsigned n = 0;
    do {
        d[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n && pos < sizeof(line) - 1)
        line[pos++] = d[--n];
}

/** 追加 v 的 8 位十六进制表示 */
static void
put_x(uint32_t v)
{
    put_s("0x");
    for (int sh = 28; sh >= 0 && pos < sizeof(line) - 1; sh -= 4)
        line[pos++] = "0123456789abcdef"[(v >> sh) & 0xFu];
}

/** 输出 line 并清空 */
static void
flush(void)
{
    line[pos] = '\0';
    (void)semihost(SYS_WRITE0, line);
    pos = 0;
}

/** 以子码 code 退出仿真中本核的报告 */
static void
report_exit(uint32_t code)
{
    uint32_t block[2] = { ADP_APP_EXIT, code };
    (void)semihost(SYS_EXIT_EXTENDED, block);
}

/** 只用移位和加法的 32 × 32 → 64 位无符号乘法 */
static __attribute__((noinline)) uint64_t
soft_mul64(uint32_t a, uint32_t b)
{
    uint64_t acc = 0;
    uint64_t sa = a;
    while (b) {
        if (b & 1u)
            acc += sa;
        sa <<= 1;
        b >>= 1;
    }
    return acc;
}

/** 只用移位和减法的无符号恢复除法；d 不为 0，余数写入 *rem */
static __attribute__((noinline)) uint32_t
soft_divu(uint32_t n, uint32_t d, uint32_t *rem)
{
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= 1u << i;
        }
    }
    *rem = r;
    return q;
}

/** 有符号除法的软件实现：商向 0 截断，余数与被除数同号 */
static int32_t
soft_div(int32_t a, int32_t b, int32_t *rem)
{
    uint32_t ua = a < 0 ? 0u - (uint32_t)a : (uint32_t)a;
    uint32_t ub = b < 0 ? 0u - (uint32_t)b : (uint32_t)b;
    uint32_t ur;
    uint32_t uq = soft_divu(ua, ub, &ur);
    *rem = (int32_t)(a < 0 ? 0u - ur : ur);
    return (int32_t)((a < 0) != (b < 0) ? 0u - uq : uq);
}

/** 递归计算 fib(n)，检查调用与栈 */
static __attribute__((noinline)) uint32_t
fib(uint32_t n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

/** 在栈上的数组中填写 base + i 并求和，与闭式结果比较；一致返回 1 */
static __attribute__((noinline)) int
stack_check(uint32_t base)
{
    volatile uint32_t buf[STACK_WORDS];
    for (uint32_t i = 0; i < STACK_WORDS; i++)
        buf[i] = base + i;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < STACK_WORDS; i++)
        sum += buf[i];
    return sum == STACK_WORDS * base + STACK_WORDS * (STACK_WORDS - 1) / 2;
}

/** 独立存储第 i 个字的花样 */
static uint32_t
store_word(uint32_t i)
{
    return ((uint32_t)CORE_ID << 24) ^ (i * 0x9E3779B1u);
}

/**
 * 整数运算检查：每轮以 LCG 推进 x，比较硬件与软件的乘法、高位乘法、
 * 无符号与有符号的除法和取余。返回通过的轮数，校验和写入 *sum
 */
static uint32_t
int_check(uint32_t *sum)
{
    uint32_t x = seed, xs = x, acc = 0, ok = 0;
    for (uint32_t i = 0; i < INT_ROUNDS; i++) {
        x = x * 1664525u + 1013904223u;
        xs = (uint32_t)soft_mul64(xs, 1664525u) + 1013904223u;
        uint32_t d = ((i * 2654435761u) >> 12) | 1u;
        uint32_t hi = (uint32_t)(((uint64_t)x * d) >> 32);
        uint32_t q = x / d, r = x % d, rs;
        uint32_t qs = soft_divu(x, d, &rs);
        int32_t sa = (int32_t)x;
        int32_t sb = (int32_t)(i & 1u ? d | 0x80000000u : d);
        int32_t sq = sa / sb, sr = sa % sb, srs;
        int32_t sqs = soft_div(sa, sb, &srs);
        int pass = x == xs &&
                   hi == (uint32_t)(soft_mul64(x, d) >> 32) &&
                   q == qs && r == rs && sq == sqs && sr == srs;
        ok += pass ? 1u : 0u;
        acc = (acc << 5 | acc >> 27) ^ x ^ q ^ r ^ hi ^ (uint32_t)sq;
    }
    *sum = acc;
    return ok;
}

/** 异常入口调用：报告 mcause、mepc 后以失败退出（crt0.S） */
void
on_trap(uint32_t mcause, uint32_t mepc)
{
    pos = 0;
    put_s("rv32[");
    put_u(CORE_ID);
    put_s("] TRAP mcause=");
    put_x(mcause);
    put_s(" mepc=");
    put_x(mepc);
    put_s("\n");
    flush();
    report_exit(EXIT_BASE | EXIT_FAIL | CORE_ID);
}

/** 程序入口（crt0.S 调用） */
int
main(void)
{
    for (uint32_t i = 0; i < STORE_WORDS; i++)
        store[i] = store_word(i);

    uint32_t sum;
    uint32_t int_ok = int_check(&sum);
    uint32_t f = fib(fib_n);
    int stack_ok = stack_check(seed);
    uint32_t store_ok = 0;
    for (uint32_t i = 0; i < STORE_WORDS; i++)
        store_ok += store[i] == store_word(i) ? 1u : 0u;
    int data_ok = data_magic == (0x5EED0000u | CORE_ID);

    int pass = int_ok == INT_ROUNDS && f == FIB_EXPECT && stack_ok &&
               store_ok == STORE_WORDS && data_ok;
    put_s("rv32[");
    put_u(CORE_ID);
    put_s(pass ? "] PASS" : "] FAIL");
    put_s(" int ");
    put_u(int_ok);
    put_s("/");
    put_u(INT_ROUNDS);
    put_s(" fib(");
    put_u(FIB_N);
    put_s(")=");
    put_u(f);
    put_s(stack_ok ? " stack ok" : " stack BAD");
    put_s(" store ");
    put_u(store_ok);
    put_s("/");
    put_u(STORE_WORDS);
    put_s(data_ok ? " data ok" : " data BAD");
    put_s(" sum=");
    put_x(sum);
    put_s("\n");
    flush();
    report_exit(EXIT_BASE | (pass ? 0u : EXIT_FAIL) | CORE_ID);
    return pass ? 0 : 1;
}
