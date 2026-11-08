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
| `start.S`、`vectors.S`、`exception.c` | 启动：设栈、设异常向量、清零 .bss、打开通用定时器的事件流（约每 10 us 一个事件）、调用 `test_main`；当前 EL（SPx）的 IRQ 进入 `bm_irq_dispatch`（`gic.h`），其余异常都打印 ESR/ELR/FAR 后以 CRASH 退出 |
| `semihost.h` | 经 Arm semihosting 输出字符串、结束仿真 |
| `console.h` | `bm_printf`、`bm_snprintf`（printf 子集，见头文件注释） |
| `test.h` | 入口约定、断言宏 `TEST_ASSERT`、`TEST_EQ`、`TEST_NE`、`TEST_FAIL`、日志宏 `TEST_INFO`、退出码 |
| `mmio.h` | `bm_read8/16/32/64`、`bm_write8/16/32/64`，自带屏障；`bm_relax()` 在轮询之间执行 `wfe`，等到下一个事件 |
| `hdi.h` | host 一侧的 HDI 驱动库：控制器的使能与 BUSY 握手、队列、SQE 与 SGL 的构造、提交、按相位位读取 CQE |
| `gic.h` | GICv3 与 ITS 的最小驱动：distributor 关闭安全扩展（DS = 1）、Group 1 以 IRQ 投递到 EL3；LPI 配置表与 pending 表、ITS 的设备表、集合表、命令队列和 ITT（放在 `platform.h` 的 `BM_GIC_MEM_BASE`）；`bm_its_map` 把（DeviceID、EventID）映射到 LPI；中断服务程序由 `bm_gic_init` 注册 |
| `pci.h` | ECAM 配置读写、按 ID 查找设备、BAR 探测与分配、使能 Command 位、遍历 capability 链 |
| `gw100.h` | GW100 的寄存器偏移与位定义（照抄 spec-pcie）；`gw100_open()` 找到设备、分配 BAR0/BAR2 并使能内存解码，`gw100_win_set()` 配置 BAR2 窗口 |
| `platform.h` | 平台地址常量（VExpress_GEM5_V2：ECAM、PCI 内存窗口、GICv3 与 ITS）；换平台只改这里 |
| `string.h` | `memcpy`、`memmove`、`memset`、`memcmp`、`strlen`、`strcmp`（编译器也会生成对它们的调用） |

## 运行环境

- 程序在 EL3 启动，单核运行，**MMU 和 cache 都关闭**。内存按 Device 类型访问，所有访问必须自然对齐，因此编译时加了 `-mstrict-align`，也不使用浮点和 SIMD（`-mgeneral-regs-only`）。
- 控制台输出走 semihosting，进入 gem5 进程的 stdout，测试运行器把它存进日志。不使用 UART。
- 设备上电后由管理 Tile 的启动 ROM 推进 STAGE，固件要由 host 加载（`spec-pcie` §6.4）。需要固件在运行的 test（例如访问 BAR2 调试窗口）先调用 `gw100_boot()`：它把嵌入的固件镜像（`firmware/` 生成的 `fw.img`，见 `lib/fw_image.S`）经启动窗口写入设备并等待 `FW_READY`。一次加载约需 170 ms 仿真时间（`fw.img` 约 10 KB），主要花在 25 MHz 下的镜像校验（SHA-256）和 SRAM、运行区的清零上，校验时间随固件大小增长（约 14 ms/KB）；`make test` 的 tick 上限为 3 s。

## test 的约定

1. **一个 test 一个文件**：`tests/<名字>.c`。需要多个源文件时用目录 `tests/<名字>/`，目录下的 `.c`、`.S` 一起链接。
2. **入口**是 `int test_main(void)`，返回 0 表示通过。
3. **互相独立**：每次运行都是一个新的 gem5 进程，test 之间不共享任何状态，也不能依赖运行顺序。
4. **出错就退出**：断言宏失败时立即打印 `文件:行` 和实际值并退出，不继续执行。
5. **只用公共库**：设备访问统一走 `mmio.h`、`pci.h`；平台地址只从 `platform.h` 取。公共库缺什么就补进 `lib/`，不在 test 里重复实现。
6. **长时间的等待让出 CPU**：等待以毫秒计的轮询（固件加载、复位、看门狗、空闲等待）在两次检查之间调用 `bm_relax()`。atomic host 的 CPU 不计访存延迟，空转的轮询在每个仿真微秒里执行大量指令，墙钟时间会增加几十倍；对延迟敏感的短等待（例如测量中断合并的时间）不用它。

**退出码**（semihosting 的退出子码，也是 gem5 的进程退出码）：

| 码 | 结果 | 含义 |
|---|---|---|
| 0 | PASS | `test_main` 返回 0 |
| 1 | FAIL | 断言失败，或 `test_main` 返回非 0 |
| 2 | CRASH | 发生未预期的异常 |
| 124 | TIMEOUT | 到达仿真 tick 上限（运行器另有墙钟超时，也记为 TIMEOUT） |
| 其他 | ERROR | gem5 因其他原因退出 |

## 配置与能力矩阵

baremetal/test-matrix.json 为每个测试声明能力和额外配置参数。
未分类测试或未知能力会使入口检查失败。默认 timing 使用 gw100，
atomic 使用 gw100_compat，分别加载匹配的固件和 ELF。
运行器逐项打印 RUN/SKIP 并报告实际执行数量。

新增 DRAM 用例归属 dram 能力。dram_access 验证容量和部分写，
dram_boot_events 验证初始化中的 FLR 与电源事件，dram_error 验证启动失败。
dram_lifecycle 在实际 COPY 已传输部分数据时注入 FLR 或 D3hot，验证旧
事务收尾和三轮数据保持。dram_link_reset 通过测试夹具的数字输入请求
PERST、Hot Reset 和 Link Down，随后重新枚举并由 ROM、固件重新训练。

PCIe 数字链路用例归属 `pcie_link` 能力，G0–G3 的状态由
`tools/pcie_spec.py --status` 查询。当前共 32 个裸机用例，timing/O3
各运行 32 项，atomic 兼容配置运行 24 项，显式跳过 8 项能力测试。
`pcie_checkpoint` 的普通回归检查访问与电源标记，三个独立进程的文件
保存和恢复由 `tools/pcie_system_verify.py --suite checkpoint` 验收。

SRAM 用例声明 `sram_timing` 能力。三种 Host 预设都使用 timing 管理 CPU
和共享 SRAM，atomic Host 继续使用匹配的兼容 DRAM 与 PCIe 档案。
`--sram-fault` 按能力矩阵选择 CE、CPU 的 UE 读取、RMW 写入、字内偏移
取指，或 ML 的描述符 UE 读取与回写。故障由硬件入口绑定并在指定事件注入。

## 现有 test

| test | 验证什么 |
|---|---|
| `hello` | 冒烟：启动、控制台输出、退出码 |
| `lib_selftest` | 公共库本身：字符串函数、格式化输出、断言宏 |
| `pcie_config` | 配置空间：ID、Class、BAR 类型与大小、PM 和 PCI Express capability 的字段（spec-pcie F1–F3） |
| `pcie_regs` | BAR0 全局寄存器、访问规则（保留偏移、非法宽度）、错误记录、中断状态寄存器、加载固件后的启动寄存器（F4–F6） |
| `pcie_bar2` | BAR2 调试窗口：配置、经窗口读写设备内存、未使能、越界、只读窗口（F7） |
| `pcie_flr` | FLR 与 D3hot→D0：配置空间、BAR、寄存器复位，设备内存保留，固件确认 FLR 后重新就绪（F11、F12） |
| `pcie_link` | 十二种速率与宽度的状态字段、MPS/MRRS/Extended Tag、BAR2 部分写、数据正确性及 Host BAR RTT，输出线上 trace |
| `pcie_link_error` | 注入实际 TLP CRC 错误，检查重放恢复、AER 状态与清除 |
| `pcie_checkpoint` | 配置、scratch、MSI-X、L0/L1 跨进程恢复标记，专用夹具完成文件往返 |
| `dram_access` | 完整容量、通道/rank、边界、部分写与拒绝语义 |
| `dram_boot_events` | 初始化期间 FLR 与 D3hot/D0 |
| `dram_error` | 指定训练失败的启动错误 |
| `dram_lifecycle` | 在途 COPY 中 FLR、电源切换与重复数据保持 |
| `dram_link_reset` | PERST、Hot Reset、Link Down 后重新枚举与固件启动 |
| `sram` | 真实固件逐 bank 的 64 B、部分写与可执行 SRAM 自检，ML 描述符往返 |
| `sram_ce` | 单比特纠正数据、饱和 CE 计数与阈值 PLIC 状态 |
| `sram_ue` | CPU UE 读取的 mcause、mtval、UE 地址及 PLIC 状态 |
| `sram_ue_store` | CPU RMW 写 UE 访问错误与异常地址 |
| `sram_ue_fetch` | 从宏字内 +2 B 执行压缩指令，验证 UE 取指错误地址 |
| `sram_ml_read` | ML 的描述符 UE 读取以 REJECT 结束，HDMA PHASE 为 1 |
| `sram_ml_write` | ML 的描述符 RMW 回写 UE 以 REJECT 结束，HDMA PHASE 为 2 |
| `boot_ok` | 上电初始化：经启动窗口加载固件，ROM 校验并跳转，固件置位 `FW_READY`（F10） |
| `boot_errors` | 启动失败：镜像校验失败、`FW_SIZE` 非法、启动窗口的非法写入，各自回到 STAGE 2 后可重新加载（F10） |
| `boot_flr` | 启动期间和固件运行期间的 FLR：门控、回到 STAGE 2 或重新就绪（F11） |
| `wdt_fatal` | 加载看门狗测试固件：第二级超时后 `DEV_FATAL`、`INT_STATUS.ERROR` 置位，心跳停止，FLR 不清除；设备完全复位后恢复（`spec-mgmt-tile` F8） |
| `hdma_basic` | 加载 HDMA 测试固件：H2D、D2H（连续缓冲与 SGL）数据正确，DEV_ADDR 错误停止后 RESUME，BAR0 镜像的计数与 `CH_ERR`（`spec-host-dma` F1、F4、F7、F9）。固件按约定的 host 地址 `BM_DMA_BUF_BASE` 访问 host 内存 |
| `msix_intx` | 不加载固件：MSI-X capability、INT_SET 与错误产生的消息、表项 Mask 与 PBA（含屏蔽期间事件被处理）、Function Mask、BME、INTx 与 Interrupt Disable（`spec-pcie` §8、F8） |
| `int_fw` | 加载中断请求的测试固件：CTX_VECTOR 映射、INT_REQ 的消息、INT_LEVEL 使清零的位重新置位（`spec-pcie` §6.5、§8.4） |
| `hdi_copy` | HDI 的基本流程（驱动库 `lib/hdi.c`）：控制器使能，管理队列的 NOP、IDENTIFY，创建上下文、设置区间，COPY_H2D（1 MiB + 4 KiB，固件拆成两个描述符）、COPY_D2H（连续缓冲与 SGL），越权、字段非法、操作码非法、SGL 长度不符的错误完成，CQ 回绕，MSI-X 完成中断，销毁上下文（`spec-hdi` F1、F3、F6、F7、F10、F12 的一部分） |
| `msix_its` | MSI-X 经 GICv3 ITS 交付给 host CPU：GW100 的 Requester ID 作为 DeviceID、事件映射到 LPI，INT_SET 产生的消息使中断服务程序被调用，屏蔽向量时只置 PBA、解除屏蔽后中断到达，HDI 管理命令的完成中断经 ITS 到达（`spec-pcie` §8.4，`spec-hdi` §12） |
| `hdi_errors` | HDI 的其余路径：FILL、TIMESTAMP，跨上下文的 SIGNAL_EVENT 与 WAIT_EVENT（满足、超时、无权限），ABORT，中断合并的 THR、TIME 与错误完成，doorbell 写入不存在的上下文与越界、上下文的错误状态、GET_LOG，管理队列 doorbell 越界的致命错误与控制器复位，正常关闭（`spec-hdi` F2、F5、F11 – F14 的一部分） |
| `dev_reset` | 设备完全复位：BAR 保留、寄存器恢复复位值，100 ms 内回到 STAGE 2，可以重新加载固件（`spec-pcie` §13.1，`spec-clock-reset-power` T3） |

Host Simple CPU 访问错误仍触发 gem5 panic，例如 MSE 关闭后的未解码 BAR
访问。管理 RISC-V CPU 已安装可选错误转换接口，通过 SRAM UE 测试验证
ISA 访问错误和异常地址。

## 运行

在仓库根目录执行：

```sh
make test                                  # 按需构建 gem5 和全部 test，在 timing、atomic 下各跑一遍
make pcie-test                            # 全部 PCIe 独立、系统与软件验收
make test TESTS="pcie_*" HOSTS=o3          # 选 test 和 host 预设，TESTS 支持通配
make run TEST=pcie_regs                    # 只运行一个 test，输出直接显示在终端
```

- 每次运行的输出在 `gem5/m5out/tests/<host>/<test>/`，其中 `test.log` 是完整日志。
- 失败时，运行器打印日志路径和测试程序的输出。
- 其他变量：`TEST_JOBS`（并行数）、`TEST_TIMEOUT`（墙钟超时，秒，默认 900；长时间的轮询不调用 `bm_relax()` 时，atomic host 的墙钟时间会大幅增加）、`TEST_MAX_TICKS`（仿真 tick 上限，默认 3×10¹²，即仿真 3 s）、`BOARD`、`DEVICE`。见 `make help`。

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
  ./build/GW100/gem5.opt --listener-mode=on ../gem5-ext/configs/run_gw100.py \
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
