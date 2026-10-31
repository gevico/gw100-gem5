# 裸机测试

在 gem5 的 ARM host 上运行的设备级测试。每个 test 是一个独立的裸机程序，共用一套公共库；顶层 `make test` 编译全部 test，在多个 host 预设下批量运行并汇总结果。

## 结构

```
baremetal/
├── Makefile        # 只负责编译：公共库编译一次，每个 test 链接成一个 ELF
├── linker.ld       # 程序从 host DRAM 0x80000000 加载运行，64 KiB 栈
├── include/bm/     # 公共头文件
├── lib/            # 公共库实现
└── tests/          # 每个 .c 是一个独立的 test
```

| 公共库 | 内容 |
|---|---|
| `start.S`、`vectors.S`、`exception.c` | 启动：设栈、设异常向量、清零 .bss、调用 `test_main`；任何异常都打印 ESR/ELR/FAR 后以 CRASH 退出 |
| `semihost.h` | 经 Arm semihosting 输出字符串、结束仿真 |
| `console.h` | `bm_printf`、`bm_snprintf`（printf 子集，见头文件注释） |
| `test.h` | 入口约定、断言宏 `TEST_ASSERT`、`TEST_EQ`、`TEST_NE`、`TEST_FAIL`、日志宏 `TEST_INFO`、退出码 |
| `mmio.h` | `bm_read8/16/32/64`、`bm_write8/16/32/64`，自带屏障 |
| `pci.h` | ECAM 配置读写、按 ID 查找设备、BAR 探测与分配、使能 Command 位、遍历 capability 链 |
| `gw100.h` | GW100 的寄存器偏移与位定义（照抄 spec-pcie）；`gw100_open()` 找到设备、分配 BAR0/BAR2 并使能内存解码，`gw100_win_set()` 配置 BAR2 窗口 |
| `platform.h` | 平台地址常量（VExpress_GEM5_Foundation）；换平台只改这里 |
| `string.h` | `memcpy`、`memmove`、`memset`、`memcmp`、`strlen`、`strcmp`（编译器也会生成对它们的调用） |

## 运行环境

- 程序在 EL3 启动，单核运行，**MMU 和 cache 都关闭**。内存按 Device 类型访问，所有访问必须自然对齐，因此编译时加了 `-mstrict-align`，也不使用浮点和 SIMD（`-mgeneral-regs-only`）。
- 控制台输出走 semihosting，进入 gem5 进程的 stdout，测试运行器把它存进日志。不使用 UART。

## test 的约定

1. **一个 test 一个文件**：`tests/<名字>.c`。需要多个源文件时用目录 `tests/<名字>/`，目录下的 `.c`、`.S` 一起链接。
2. **入口**是 `int test_main(void)`，返回 0 表示通过。
3. **互相独立**：每次运行都是一个新的 gem5 进程，test 之间不共享任何状态，也不能依赖运行顺序。
4. **出错就退出**：断言宏失败时立即打印 `文件:行` 和实际值并退出，不继续执行。
5. **只用公共库**：设备访问统一走 `mmio.h`、`pci.h`；平台地址只从 `platform.h` 取。公共库缺什么就补进 `lib/`，不在 test 里重复实现。

**退出码**（semihosting 的退出子码，也是 gem5 的进程退出码）：

| 码 | 结果 | 含义 |
|---|---|---|
| 0 | PASS | `test_main` 返回 0 |
| 1 | FAIL | 断言失败，或 `test_main` 返回非 0 |
| 2 | CRASH | 发生未预期的异常 |
| 124 | TIMEOUT | 到达仿真 tick 上限（运行器另有墙钟超时，也记为 TIMEOUT） |
| 其他 | ERROR | gem5 因其他原因退出 |

## 现有 test

| test | 验证什么 |
|---|---|
| `hello` | 冒烟：启动、控制台输出、退出码 |
| `lib_selftest` | 公共库本身：字符串函数、格式化输出、断言宏 |
| `pcie_config` | 配置空间：ID、Class、BAR 类型与大小、PM 和 PCI Express capability 的字段（spec-pcie F1–F3） |
| `pcie_regs` | BAR0 全局寄存器、访问规则（保留偏移、非法宽度）、错误记录、中断状态寄存器、启动寄存器（F4–F6） |
| `pcie_bar2` | BAR2 窗口：配置、经窗口读写设备内存、未使能、越界、只读窗口（F7） |
| `pcie_flr` | FLR 与 D3hot→D0：配置空间、BAR、寄存器复位，设备内存保留（F11、F12 的寄存器部分） |

**已知限制**：gem5 的 Simple CPU（timing、atomic）在访存收到错误响应时直接 panic，不会产生 data abort。因此测试无法验证"访问未解码地址会出错"这类行为，例如关闭 Memory Space 后再访问 BAR；这类检查只能验证配置寄存器本身。

## 运行

在仓库根目录执行：

```sh
make test                                  # 按需构建 gem5 和全部 test，在 timing、atomic 下各跑一遍
make test TESTS="pcie_*" HOSTS=o3          # 选 test 和 host 预设，TESTS 支持通配
make run TEST=pcie_regs                    # 只运行一个 test，输出直接显示在终端
```

- 每次运行的输出在 `gem5/m5out/tests/<host>/<test>/`，其中 `test.log` 是完整日志。
- 失败时，运行器打印日志路径和测试程序的输出。
- 其他变量：`TEST_JOBS`（并行数）、`TEST_TIMEOUT`（墙钟超时，秒）、`TEST_MAX_TICKS`（仿真 tick 上限，默认 10¹⁰，即仿真 10 ms）、`BOARD`、`DEVICE`。见 `make help`。

只编译不运行：

```sh
make -C baremetal                  # 编译全部 test
make -C baremetal TEST=pcie_regs   # 只编译一个
make -C baremetal list             # 列出全部 test
make -C baremetal disasm TEST=hello
```

## 新增一个 test

1. 新建 `tests/<名字>.c`，按 `tools/templates/file-header.hh` 写文件头，实现 `int test_main(void)`。每个函数、宏函数和文件作用域变量都要有 `/** ... */` 注释，`test_main` 的注释与 `test.h` 中声明处相同（`CONTRIBUTING.md` §7）。
2. 用 `test.h` 的断言宏检查结果，用 `TEST_INFO` 打印关键过程，便于失败时定位。
3. `make test TESTS=<名字>` 验证通过；再让它故意失败一次，确认失败信息能指出问题。

不需要修改任何 Makefile 或运行器，新文件会被自动发现。
