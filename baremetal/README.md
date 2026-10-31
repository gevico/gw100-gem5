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

## 调试

模型（gem5 的 C++ 代码）和裸机程序可以同时调试：左边用 lldb 调试 gem5 进程，右边用 aarch64 gdb 经 gem5 的远程 GDB 调试裸机程序。两者在同一个仿真进程里，共用同一条时间轴。

- 右边停在断点时，仿真时间冻结，左边仍可查看设备的全部状态。
- 左边停在断点时，整个 gem5 进程冻结，右边会等待，直到左边继续。典型的用法是在右边单步执行一条访问设备的指令，左边停在设备处理这次访问的代码里（例如 `Bar0Handler::access`）。
- 远程 GDB 用 `127.0.0.1:7100`。不用 gem5 默认的 7000，也不要写 `localhost`：macOS 的"隔空播放接收器"监听 `*:7000`，`localhost` 解析到 `::1` 时会连到它上面并挂住。
- macOS 上左边只能用 lldb：GDB 不支持在 Apple Silicon 上调试本机进程。

**在 VS Code 中**（两侧都用 C/C++ 扩展 `ms-vscode.cpptools` 的 `cppdbg` 调试器：左边 `MIMode: lldb`，用扩展自带的 lldb-mi；右边 `MIMode: gdb`，用 aarch64 gdb）：

1. 启用 C/C++ 扩展。代码提示仍用 clangd，`make vscode-config` 会关闭 cpptools 的 IntelliSense，两者不冲突。
2. `make vscode-config TEST=pcie_regs`：生成 `.vscode/launch.json` 和 `tasks.json`；要调试的 test 记在工作区设置 `gw100.debugTest` 中，之后可以直接改这一项。
3. 先 `make gem-build baremetal-build`。
4. 在 `gem5-ext/src/` 和 `baremetal/` 的源码里设断点，启动"GW100 联合调试"。左边的 lldb-mi 启动 gem5 并等待 GDB，右边的 gdb 等 gem5 开始监听后连上，默认停在 test 的入口 `test_main`。入口函数由工作区设置 `gw100.debugEntry` 指定；要从第一条指令（`start.S` 的 `_start`）开始调试，把生成的配置中的 `stopAtConnect` 改为 `true`。aarch64 gdb 由 `make vscode-config` 自动查找（`aarch64-linux-gnu-gdb` 或 `gdb-multiarch`），也可以用 `AARCH64_GDB=` 指定。

**在命令行中**（两个终端）：

```sh
# 终端 1：lldb 调试 gem5
cd gem5
lldb -o "process handle SIGIO -s false -n false -p true" \
     -o "breakpoint set -n gem5::gw100::Bar0Handler::access" -o run -- \
  ./build/ARM/gem5.opt --listener-mode=on ../gem5-ext/configs/run_gw100.py \
  --kernel ../build/gw100-baremetal/pcie_regs.elf --wait-gdb

# 终端 2：aarch64 gdb 调试裸机程序
aarch64-linux-gnu-gdb build/gw100-baremetal/pcie_regs.elf \
  -ex "target remote 127.0.0.1:7100" -ex "tbreak test_main" -ex continue
```

只调试裸机程序时，第一步改为 `make run TEST=pcie_regs GEM5_OPTS="--listener-mode=on" CONFIG_ARGS="--wait-gdb"`，不需要 lldb。

注意：

- gem5 用 `SIGIO` 处理监听套接字的事件，lldb 默认会因它停住进程，所以要用 `process handle SIGIO` 放行（VS Code 配置已包含）。
- 不要在 VS Code 的断点面板中勾选 **"All C++ Exceptions"**（默认不勾选）。gem5 的远程 GDB 服务用 C++ 异常处理客户端发来的不支持的查询：每次连接、每次裸机程序停下，都会抛出并捕获几十个异常。勾选时左边会反复停在 `__cxa_throw`，右边随之等待，看起来像卡死。这与 macOS 无关，在 Linux 上开启"抛出时中断"也一样。
- `gem5.opt` 是优化构建，lldb 中部分变量显示为 `<unavailable>`。
- 裸机程序用 `-O2` 编译，gdb 单步时可能在源码行之间跳转。

## 新增一个 test

1. 新建 `tests/<名字>.c`，按 `tools/templates/file-header.hh` 写文件头，实现 `int test_main(void)`。每个函数、宏函数和文件作用域变量都要有 `/** ... */` 注释，`test_main` 的注释与 `test.h` 中声明处相同（`CONTRIBUTING.md` §7）。
2. 用 `test.h` 的断言宏检查结果，用 `TEST_INFO` 打印关键过程，便于失败时定位。
3. `make test TESTS=<名字>` 验证通过；再让它故意失败一次，确认失败信息能指出问题。

不需要修改任何 Makefile 或运行器，新文件会被自动发现。
