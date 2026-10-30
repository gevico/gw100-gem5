/**
 * @file  semihost.h
 * @brief Arm semihosting：控制台输出与退出
 *
 * 通过 `hlt #0xf000` 陷入 gem5。AArch64 下参数放在 x1 指向的内存块里。
 * 输出进入 gem5 进程的 stdout，测试运行器据此收集日志；退出时的子码
 * 成为 gem5 的进程退出码（见 gem5-ext/configs/run_gw100.py）。
 */

#ifndef BM_SEMIHOST_H
#define BM_SEMIHOST_H

/* 输出一个以 0 结尾的字符串 */
void bm_semihost_write0(const char *s);

/* 以 ADP_Stopped_ApplicationExit 结束仿真，code 为退出码；不返回 */
__attribute__((noreturn)) void bm_semihost_exit(unsigned long code);

#endif /* BM_SEMIHOST_H */
