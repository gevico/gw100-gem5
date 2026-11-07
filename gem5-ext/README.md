# gem5-ext：GW100 的 gem5 模型

通过 `EXTRAS` 编译进上游 gem5 的 ARM 构建（host 为 AArch64），不修改 `gem5/`。完整的开发者指南将写成 `man-gem5-dev`。

## 构建

命令都从仓库根目录开始执行。第一次构建是全量的，要十分钟以上；之后只改 gem5-ext 时，增量构建一两分钟。改了 C++ 或 SimObject 的 Python 参数之后要重新构建：`make gem-build`、`make run`、`make test` 会比较 `gem5-ext/src/` 下的源文件、`gem5-ext/patches/` 下的补丁和 gem5 子模块的提交号，有变化才调用 scons，没有变化时直接跳过。调用 scons 之前先应用 `gem5-ext/patches/` 中的 gem5 本体补丁（已经打过的跳过），说明见该目录的 README；应用后主仓库的 `git status` 显示 `gem5 (modified content)` 是预期的。改了 `PYTHON_CONFIG` 等构建选项时，用 `make -B gem-build` 强制构建。只改 `gem5-ext/configs/` 下的配置脚本不需要重新构建。

```sh
uv pip install --python .venv/bin/python -r requirements-dev.txt   # 含 scons
make check        # 检查工具链、Python，以及 gem5.opt 链接的 Python 版本
make gem-build    # 构建 gem5/build/GW100/gem5.opt 和 compile_commands.json
```

`build/GW100` 同时编译 ARM 和 RISC-V 两个 ISA：ARM 用于 host，RISC-V 用于管理 Tile 和 Compute Tile 的控制核，两者运行在同一个仿真进程中。第一次构建时，`make gem-build` 以 `build_opts/ARM` 为基础生成构建配置，再打开 `USE_RISCV_ISA`。

gem5 嵌入的 Python 要与 `.venv` 同一版本（3.12），`make gem-build` 会自动找对应的 `python3.12-config`。手工构建时注意：gem5 只从**环境变量**读 `PYTHON_CONFIG`，写成 scons 参数不生效，会退回系统默认的 `python3-config`（macOS 上是 Homebrew 的 3.14）：

```sh
cd gem5
../.venv/bin/scons defconfig build/GW100 build_opts/ARM EXTRAS="$PWD/../gem5-ext"
../.venv/bin/scons setconfig build/GW100 USE_RISCV_ISA=y
PYTHON_CONFIG=/opt/homebrew/bin/python3.12-config ../.venv/bin/scons \
    --ignore-style build/GW100/gem5.opt EXTRAS="$PWD/../gem5-ext" -j8
```

Linux（以 Ubuntu 24.04 为例）：先装好 `build-essential m4 scons python3-dev`，这是 gem5 上游列出的最小依赖（`gem5/util/dockerfiles/ubuntu-24.04_min-dependencies/Dockerfile`），裸机程序另需 `gcc-aarch64-linux-gnu`，然后同样执行 `make gem-build`。

## 建模约定

建模的最高原则见 [CONTRIBUTING.md](../CONTRIBUTING.md) §5：模型是在模拟硬件，不能为了软件或测试的方便脱离实际。在此之下：

- 代码全部放在 `gem5-ext/`，通过 `EXTRAS` 编进 gem5，不改 gem5 本体。确实绕不开的本体改动，做成补丁放进 `gem5-ext/patches/`，并注明原因。
- **目录树即组件树**：一个目录是组件树上的一个节点，放这个节点自己的类；只有一个类的叶子组件作为文件放在父目录中，自身还有子组件的建子目录；被多个子树共用的组件放在它们的最近公共祖先下。组件之间的接口放在共同父节点的 `*_types.hh` 中。每个目录一个 `SConscript`，只声明本目录的源文件；单元测试不放在 `src/` 中（见"测试"）。目录关系因此就是组成关系；跨子树的引用（例如 `bar0/window_regs` 使用 `window/window_table`）只经头文件，并在类注释中写明。
- 按张量指令粒度建模，不逐个 MAC 仿真。
- ISA 不放进 `gem5/src/arch`：张量指令由 Tile 的 SimObject 自己译码；编码将由重建后的单一真源生成（旧的 `isa_defs.hh` 已随 func-core 删除）。
- 片上网络计划用自研的轻量 2D Mesh，粒度是 DMA 传输，支持多播；Garnet 只用来交叉验证。Compute Tile 重建之前还没有片上互连。
- 通用 LPDDR5 性能预设位于 src/memory/，当前提供
  LPDDR5_8433_1x16_BG_BL32，继承上游 LPDDR5-5500。参数依据见
  [spec-dram 的通用性能附录](../docs/spec/memory/spec-dram/lpddr5-8433.md)。

## 组件与规格的对应

| 目录 | SimObject / 模块 | 对应规格 | 状态 |
|---|---|---|---|
| src/memory/ | LPDDR5_8433_1x16_BG_BL32，复用上游 DRAMInterface 与 MemCtrl | spec-dram 通用性能附录 | 独立性能预设，使用下述配置验证 |
| `src/gw100/pcie/` | `Gw100PciDevice`：外壳，只负责 PciEndpoint 的入口、`mem_port`、ML 的 HIF 侧接入、功能级复位与设备完全复位的编排（`dev_reset_req` 请求 CRC，`func_reset` 接受复位）、看门狗的 `wdt_fatal` 信号和 checkpoint；`Gw100PcieFactory` 组装下列组件，接口见 `pcie_types.hh`。`status/`：`StatusUnit`（ERR/INT）；`boot/`：`BootCtrl`（STAGE、启动窗口、提交计数、FW_START 门控、启动通路故障、FLR 门控与确认）；`hif/`：`HifRegs`（固件可见的 HIF 寄存器区，ML 的 HIF 侧目标）；`window/`：`WindowTable`（BAR2 调试窗口）；`config/`：`ConfigSpace`，其下 `capability/`：`Capability` 基类、`PmCapability`、`PcieCapability`、`MsixCapability`（0x90）；`bar0/`：`Bar0Handler` 与 `GlobalRegs`、`WindowRegs`、`BootRegs`、`HdmaMirror`（HDMA 寄存器的只读镜像）；`bar2/`：`Bar2Handler`（启动窗口、调试窗口、关闭）、`DevmemBackend`、`Gw100PrefetchMemBar`；`shim/`：`ResetShim`（设备完全复位期间的 reset shim）；`intr/`：`IntCtrl`（MSI-X 的待发、来源、粘滞 Pending 与发送，INTx 电平，`CTX_VECTOR`）与 BAR0 的 `MsixTable`、`MsixPba`；`hdi/`：`HdiCtrl`（HDI 控制器寄存器的影子、`HDI_WR_MAP` 与 `HDI_WR` 事件、BUSY 握手、doorbell 待发槽按最后一次写入的先后发出 `DB_WR`）与 BAR0 的 `HdiRegsBlock`、`DoorbellPage`；`hdma/`：`HdmaEngine`（Host DMA：描述符环、SGL、仲裁、tag 与有限缓冲、错误停止与中止），经 `HdmaClock`、`HostDmaPort`（dma 端口，TLP 按 cache 行拆成内存包）、`MlHdmaClient` 和 `DevmemBackend` 的 timing 接口接入 | `spec-host-dma`、`spec-pcie`、`spec-clock-reset-power` | 寄存器层与启动流程完成：裸机测试 `boot_ok`、`boot_errors`、`boot_flr` 覆盖 F10、F11 的启动部分，`dev_reset` 覆盖设备完全复位，`pcie_*` 覆盖 F1–F7、F11/F12 的寄存器部分；`hdma_basic` 覆盖 HDMA 的连续缓冲、SGL、错误停止与 BAR0 镜像；`msix_intx`、`int_fw` 覆盖 MSI-X、INTx 与固件的中断请求；`ConfigSpace`、`WindowTable`、`StatusUnit`、`ResetShim`、`HdmaEngine`、`IntCtrl`、`HdiCtrl` 有单元测试。`hdi_copy`、`hdi_errors` 覆盖 HDI 的 HIF 侧与固件的命令处理（LOAD_PROGRAM、LAUNCH 等待 Compute Tile） |
| `src/gw100/ml/` | `Gw100MgmtLink`：ML 的两端适配器 `MlEndpoint` 与四个 `AsyncFifo`，两端分别由 PCIe 设备（`clk_pcie`）和管理 Tile（`clk_mgmt`）驱动 | `spec-mgmt-link` | 完成；`MlEndpoint` 有单元测试（建立、顺序、tag、屏障、撤销、超时与故障、延迟与吞吐） |
| `src/gw100/mgmt/` | `Gw100MgmtTile`：外壳，本地总线端口、ML 管理侧目标、PLIC 中断源、NI 发起端口（本地地址 0x100_0000_0000 + DPA）。`sram/`：`MgmtSram`（ROM、按 bank 排队的 SRAM、复位后的硬件清零）；`mailbox/`：`MgmtMailbox`（doorbell 信箱、事件寄存器）；`regs/`：`MgmtLocalRegs`（ML、SRAM 控制、访问控制）；`wdt/`：`MgmtWdt`（看门狗的两级超时状态机；外壳按同步后的 `mtime` 安排到期，第二级时扣下 CPU 的取指并经专用信号 `wdt_fatal` 置位 HIF 的 `DEV_FATAL`）；`crc/`：`MgmtCrc`（复位原因、PLL 锁定、无毛刺时钟切换；切换 `clk_mgmt` 时改变管理系统时钟域的频率档）；`intr/`：`MgmtInterrupts`（RISC-V 中断控制器加上停机请求，CPU 在指令边界停住）。外壳还实现 CRC 的设备完全复位时序：复位本 Tile、ML 两端和 PCIe 的功能逻辑，释放后让 CPU 从启动 ROM 重新开始。管理 CPU（`RiscvTimingSimpleCPU`，RV64，M/U）、CLINT、PLIC 用 gem5 的设备，在 `configs/gw100/devices/mgmt.py` 中组成独立的 RiscvSystem | `spec-mgmt-tile` | 启动相关部分完成，运行真实的 RISC-V 启动 ROM 与固件（`firmware/`）；时钟切换、看门狗、设备完全复位完成；`MgmtCrc`、`MgmtWdt` 有单元测试；NoC 端口未实现 |
| （待建）`src/noc/` | 轻量 2D Mesh，支持多播 | `spec-noc` | 未开始 |

原来的 func-core、Compute Tile 和 common 已删除，按新的规格逐步重建。

## 通用 LPDDR5-8433

执行 make gem-build 后，可在 gem5 配置中通过 m5.objects 使用该类。

~~~python
from m5.objects import AddrRange, LPDDR5_8433_1x16_BG_BL32

dram = LPDDR5_8433_1x16_BG_BL32(range=AddrRange("1GiB"))
controller = dram.controller()
~~~

默认组织和纳秒级阵列访问时序沿用上游 LPDDR5-5500，CK、分段 burst、
命令间隔和 tRTP 按指定的 8433 MT/s 重算。参数源为
[LPDDR5.py](src/memory/LPDDR5.py)，推算规则见上述规格附录。

独立 timing 验证覆盖预热后的行命中延迟，以及保留刷新的读、写和混合
流量，参数量化和手算对照输出到终端，完整统计保存在各自输出目录。

~~~sh
gem5/build/GW100/gem5.opt --outdir=build/lpddr5-8433-latency \
  gem5-ext/configs/feasibility/lpddr5_8433.py
gem5/build/GW100/gem5.opt --outdir=build/lpddr5-8433-read \
  gem5-ext/configs/feasibility/lpddr5_8433.py --mode read
gem5/build/GW100/gem5.opt --outdir=build/lpddr5-8433-write \
  gem5-ext/configs/feasibility/lpddr5_8433.py --mode write
gem5/build/GW100/gem5.opt --outdir=build/lpddr5-8433-mixed \
  gem5-ext/configs/feasibility/lpddr5_8433.py --mode mixed
~~~

## 测试

设备级测试是在 host 上运行的裸机程序，放在 `baremetal/tests/`，每个 test 独立运行，共用 `baremetal/lib/` 的公共库。在仓库根目录执行 `make test`，默认在 `timing` 和 `atomic` 两个 host 预设下各跑一遍；架构、约定和用法见 `baremetal/README.md`。

组件的单元测试用 gem5 自带的 GTest，放在 `tests/unit/` 下，目录结构与 `src/` 一一对应，例如 `src/gw100/pcie/config/config_space.cc` 的测试是 `tests/unit/gw100/pcie/config/config_space.test.cc`。只为不依赖 gem5 `Packet` 的纯逻辑组件写单元测试，例如 `ConfigSpace`、`WindowTable`、`StatusUnit`。

测试按标签引用被测的源文件，不写相对路径：组件的 `SConscript` 用 `Source(..., add_tags='gw100 pcie config')` 给源文件打标签，测试目录的 `SConscript` 用 `GTest(..., with_tag('gw100 pcie config'), with_tag('gem5 serialize'))` 取用。组件移动位置时测试不受影响。

构建并运行一个测试（在 `gem5/` 目录，`PYTHON_CONFIG` 与 `make gem-build` 相同）：

```sh
scons --ignore-style EXTRAS=<仓库根目录>/gem5-ext \
    build/GW100/gem5-ext/tests/unit/gw100/pcie/config/config_space.test.opt
./build/GW100/gem5-ext/tests/unit/gw100/pcie/config/config_space.test.opt
```

枚举、构建并运行全部 GTest（在仓库根目录，先 `make gem-build`）：

```sh
.venv/bin/python tools/gtest.py
```

脚本从 `tests/unit/` 下各 `SConscript` 的 `GTest(...)` 声明枚举程序，与 `.test.cc` 的文件数对照，用一次 scons 构建全部程序后逐个运行。最后一行汇总发现、构建、运行、通过的程序数和用例的通过数 / 总数，任何一项不一致都算失败（退出码 1）。新增的测试只要在 `SConscript` 中声明就会被纳入，不用改脚本；`--no-build` 只运行已构建的程序。

全栈 E2E 测试（应用 → runtime → 驱动 → Linux → PCIe → 设备）的方案尚未落地。原有的 `tests/l1_inst/`、L0 单元测试、TestHost 和 testkit 已于 2026-10-24 删除，不要恢复。

## 开发注意

- PCIe endpoint 继承 `PciEndpoint`，Host 使用 `VExpress_GEM5_V2` 的 `GenericArmPciHost`（ECAM 在 0x3000_0000，内存窗口为总线地址 0 – 0x3FFF_FFFF、CPU 地址 0x4000_0000 起的 1 GiB）；BAR0 为 1 MiB，BAR2 为 64 MiB（spec-pcie §5）。
- `VExpress_GEM5_V2` 带 GICv3（GICv4）和 ITS（0x2E01_0000），设备的 MSI-X 消息写到 `GITS_TRANSLATER` 时由 ITS 翻译成 LPI 交给 CPU：设备发往 host 的每个请求都以 PCIe 的 Requester ID 作为 stream ID，ITS 据此得到 DeviceID；裸机板卡的 DMA bridge 除内存外还转发 ITS 的翻译页。裸机库 `bm/gic.h` 配置 GIC 与 ITS，`msix_its` 检查中断服务程序被调用；其余 MSI-X 测试把表项地址指向 host 内存，只检查设备一侧的语义。INTx 经 GIC 的 SPI（`int_base` 100 加设备号模 4）。gem5 的 ITS 需要 `gem5-ext/patches/` 中的两个补丁（Fiber 在 macOS 上的布局问题、DS = 1 时 EOIR1 的分组）。
- gem5 的 `PciDevice` 对 16 位的 Command 写只取低 8 位（Interrupt Disable 会丢失），`Gw100PciDevice::writeConfig` 把它转成 32 位写交给基类。
- 旧的 TestHost 和 device-side 配置已删除；GW100 现在走 ARM board 的 VExpress PCIe。

## 理想化清单

原则见仓库根目录 `CLAUDE.md`：模型是在模拟硬件，凡是与真实硬件不一致的简化都必须登记在这里。"方向"指这项简化让性能结果偏乐观还是偏悲观。L2 校准前要先消除标为"高"的项，否则校准的是一个不符合物理的模型。

编号沿用，不复用；`Idealization:` 脚注引用的是这里的编号。几项复合的条目拆成可以分别验收和关闭的子项（例如 10a），子项沿用原编号加字母；只有一个子项真正完成并验证后才能删去它，不整行提前删除。

| # | 位置 | 现状 | 方向 | 优先级 | 计划 |
|---|---|---|---|---|---|
| 2a | PCIe 通路：host 发起的访问 | VExpress 的 PCIe 是功能模型，host 的 BAR 访问没有 Gen4 x4 的链路时序和带宽，固定花 `pioDelay` | 未知 | 中 | 加 PCIe 链路 shim；本窗口的处置待决定（Q1） |
| 2b | PCIe 通路：设备发起的 TLP | HDMA 的 TLP 按 cache 行直接进入 host 内存系统，没有链路延迟和带宽限制 | 乐观 | 中 | 同 2a |
| 2c | PCIe 通路：流控 | 没有 posted、non-posted、completion 的流控信用；发送通路只以 `pcie_tx_credits`（16 个 TLP，推算）近似 replay buffer | 乐观 | 中 | 同 2a |
| 9 | BAR2 窗口 | 经 `mem_port` 以功能访问读写设备内存，固定花 `bar2_latency`（200 ns），不与设备内部的其他访问竞争 | 乐观 | 中 | 性能建模阶段改为 timing 访问 |
| 10a | 设备内存：DRAM | 用 `SimpleMemory` 代替 LPDDR5X（32 GiB），没有 DRAM 的时序、带宽、通道和刷新 | 乐观 | 高 | M7 接入 Memory Tile 与 LPDDR5X 控制器模型（`spec-dram`） |
| 10b | 设备内存：互连 | PCIe 设备的 `mem_port`（BAR2、HDMA）和管理 Tile 的 NI 经一个交叉开关访问设备内存（clk_core 1 GHz、32 B 宽、每段 2 个周期，推算），代替 Mesh NoC | 乐观 | 高 | M6 接入 NoC（`spec-noc`） |
| 10c | 设备内存：HDMA 的在途上限 | `devmem_max_outstanding` = 32 是假定的 NoC 端口缓冲，没有规格依据 | 未知 | 中 | M6 随 `spec-noc` 的在途上限（G-N4）确定 |
| 10d | 设备内存：受保护区域 | 设备内存的受保护区域不检查（`spec-address-map` §5.3） | 乐观 | 高 | M7 随 Memory Tile 实现 |
| 12a | FLR 的时间 | FLR 立即完成，不占时间 | 乐观 | 低 | 加复位时间参数 |
| 12b | D3hot→D0 的时间 | D3hot→D0 的复位立即完成，不占时间 | 乐观 | 低 | 同 12a |
| 12c | 复位时的在途事务 | 复位时不建模"停止发起、排空、丢弃在途请求、处理迟到的响应"；正确性在于这些行为，不在于加一个延迟常数 | 乐观 | 中 | 先做新接入路径（NoC、Memory Tile）需要的部分，随 M6、M7 实现 |
| 13a | PCIe capability：错误报告 | 已实现 PM、PCI Express 和 MSI-X；AER 未实现，Device Status 的错误位从不置位 | 功能缺失 | 低 | 随错误处理实现；本窗口的处置待决定（Q2） |
| 13b | PCIe capability：Transactions Pending | Device Status 的 Transactions Pending 从不置位 | 功能缺失 | 低 | 随 12c 的排空行为实现 |
| 13c | PCIe capability：链路相关 | DSN、LTR、Secondary PCIe 与 Gen4 物理层 capability 未实现 | 功能缺失 | 低 | 随链路建模（2a–2c）实现 |
| 15 | CLINT | 用 gem5 的 Clint：`mtime` 可被写入（规格要求只读）；`PLL_TS` 的锁定延迟不建模，时间戳从仿真开始即计数。看门狗按仿真时间换算 `mtime`，固件写 `mtime` 后两者不一致 | 乐观 | 低 | 换成自己的 CLINT，按规格只读 |
| 16 | ML 客户端队列 | 启动窗口的合法写入拆成 `BOOT_WR`、doorbell 的待发槽发出 `DB_WR` 后，进入 ML 客户端队列，队列不设上限，不反压 host；当前 host 一次只有一个未完成的 MMIO 写，doorbell 又按槽合并，不会积压 | 乐观 | 中 | 待批准；或建模有限的入站缓冲和对 host 的反压 |
| 19a | 管理 Tile 的 SRAM ECC | 校验位不建模，没有故障注入，不会产生可纠正或不可纠正错误；相关寄存器存在 | — | 低 | 作为独立的修复包，插入位置待第 3 天的重估；包括部分写的读改写、CE/UE 与错误传播 |
| 19b | Compute Tile 的 SRAM ECC | 尚无模型 | — | 中 | M8 从设计开始覆盖，不重犯 19a |
| 20a | CRC：NoC | `CRC_MESH_RST` 按规格读写，但没有 NoC 模型可以作用；`clk_core` 的切换只改寄存器 | 功能缺失 | 中 | M6：NoC 上电保持复位，由固件释放 |
| 20b | CRC：DDR 与 Memory Tile | `PLL_DDR` 锁定后不驱动任何时钟，没有 Memory Tile 可以复位 | 功能缺失 | 中 | M7 |
| 20c | CRC：Compute Tile | `CRC_CT_RST`、`CRC_CT_CLK_EN`、`CRC_CT_ISO` 按规格读写，但没有 Compute Tile 模型可以作用 | 功能缺失 | 中 | M8 |
| 20d | CRC：NI 隔离的应答 | `CRC_ISO_ACK`、`CRC_ISO_FAIL` 读 0 | 功能缺失 | 中 | 随 NoC 的 NI 隔离（M6）与各 Tile 的接入 |
| 21 | 管理 CPU 的停机 | gem5 的 `TimingSimpleCPU` 不能在任意时刻停住。停机请求经中断控制器 `Gw100MgmtInterrupts` 在下一个指令边界取走，优先于全部中断，CPU 随后去取启动 ROM 末尾保留的字，管理 Tile 扣下这次取指；设备完全复位时再用复位向量处的指令响应它。与规格的差别：停机请求到达时已经在途的取指，其指令仍会执行（规格要求丢弃）；没有调试模块，停住后无法读取 CPU 状态 | 功能偏差 | 低 | 待批准；或加调试模块，换用能丢弃在途取指的 CPU 模型 |
| 22 | 设备完全复位中的 PLIC 与 CLINT | gem5 的 PLIC 没有复位接口，`Clint` 的复位会把 `mtime` 一起清零（规格要求设备完全复位保留 `mtime`），所以两者都不复位：PLIC 的使能、优先级、阈值，CLINT 的 `mtimecmp`、`msip` 保留复位前的值。启动 ROM 不使用它们；CPU 复位后 `mstatus.MIE`、`mie` 为 0，固件在 `irq_init` 中重新设置 PLIC 的优先级、使能、阈值和 `mtimecmp` 之后才开中断，旧值不会引发中断 | 功能偏差 | 低 | 随自己的 CLINT（#15）一起实现 PLIC 与 CLINT 的复位 |
| 23 | HDMA 的设备侧地址 | DPA 只接受设备内存的范围，Compute Tile 的 SRAM 尚未建模，指向它的描述符按 DEV_ADDR 失败；DEV_ECC 不会出现（#19） | 功能缺失 | 低 | 随 NoC 与 Compute Tile 的模型接入 |
| 24 | Requester ID 的总线号 | 真实的 endpoint 从配置写请求中捕获自己的总线号；gem5 的配置访问不携带总线号，Requester ID（ITS 的 DeviceID）的总线号由 SimObject 参数 `pci_bus` 给出（默认 0，VExpress 的设备都在根总线上），设备号和功能号取自 gem5 的 PCI 地址。设备挂在桥后面（总线号不为 0）而参数没有相应设置时，DeviceID 会错 | 功能偏差 | 低 | 待批准；或在 gem5 的配置访问中携带总线号后改为捕获 |

随旧模型删除而失效（2026-10-24），重建对应组件时不得重犯：

| # | 原位置 | 当时的理想化 | 重建时的要求 |
|---|---|---|---|
| 1 | NoC | 用 `NoncoherentXBar` 代替 2D Mesh，没有多跳延迟和链路竞争 | 自研轻量 Mesh（`spec-noc`） |
| 3 | DMA 暂存缓冲 | 按整次传输大小分配，读方从不被反压 | 有限暂存缓冲，读方受剩余空间限制 |
| 4 | 向量引擎 | 操作数整块读入不限大小的缓冲，读、算、写不重叠 | 有限缓冲的流式流水线，缓冲大小进架构参数 |
| 5 | 派发器 | 整个程序读进不限大小的缓冲，零时间译码和校验 | 有限指令缓冲、流式取指，译码花周期；整程序检查移到软件 |
| 6 | 指令多播 | 多播指令无论发给多少 Tile 都只占一次指令通道 | 定下指令通道的物理形态后，按该形态计时 |
| 7 | DRAM 时序 | LPDDR5X-8533 由 LPDDR5-6400 派生，tCL 沿用 6400 的纳秒值 | 按 JEDEC LPDDR5X 规范逐项核对 |
| 8 | 向量引擎吞吐 | 各数据类型都按 32 元素/周期计 | 按 `spec-vector-engine` 定义的各类型吞吐 |

已消除：
- 2026-11-03：启动寄存器上电即为 STAGE 4 且 `FW_READY`、调试窗口始终开启（原 #11）。现在由真实的 RISC-V 启动 ROM 推进 STAGE，host 经启动窗口加载固件，固件置位 `FW_READY` 并开启调试窗口。
- 2026-10-18：DRAM 容量随测试缩小，器件规格也跟着变。现在只取自架构参数。
- 2026-10-18：派生 LPDDR5X 时把 tCL 也按时钟缩短了，让 DRAM 延迟凭空变小。现在 tCL 保持纳秒值不变。
