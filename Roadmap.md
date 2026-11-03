# Roadmap：上电到算子执行的端到端纵切

## 目标

7 天内在 gem5 中打通一条功能级的端到端链路：

1. **上电初始化**：设备上电，启动 ROM 运行，host 驱动经 BAR2 加载固件，固件完成初始化并置位 `FW_READY`。
2. **驱动逻辑**：host 驱动初始化命令协议 HDI，创建上下文，经 HDMA 在 host 内存与设备内存之间搬运数据。
3. **算子执行**：驱动加载并启动一个程序，管理 Tile 固件启动各 Compute Tile 上的 kernel，Tile 控制核向引擎发命令完成计算，结果与 host 参考实现**按位一致**。

本文件是第一阶段（打通链路，允许登记过的理想化）的逐日计划；把整条链路做到不依赖理想化的完整要求见 `Goal.md`。

本阶段只要求**功能正确**，不追求时序和性能精度。为赶进度所做的简化，全部登记进 `gem5-ext/README.md` 的理想化清单。

### 完成标准

- `make test` 全部通过，其中包括一个端到端测试：上电 → 加载固件 → 初始化 HDI → 创建上下文 → 搬入数据 → 运行算子 → 搬回结果 → 与参考结果比较。
- 固件是**真实的 RISC-V 程序**，由 `riscv64-elf-gcc` 编译，运行在 gem5 模拟的 RISC-V 管理核上。
- 新增和修改的部件都有 GTest 单元测试或裸机测试。

---

## 名词：固件与驱动

| | 固件 | 驱动 |
|---|---|---|
| 运行在哪里 | GW100 内部，管理 Tile 的 RISC-V 核（RV64IMAC） | host 的 CPU（本仓库中是 gem5 模拟的 ARM host） |
| 驱动谁 | GW100 内部的硬件：HIF 寄存器、HDMA、NoC、Memory Tile、各 Compute Tile | 隔着 PCIe 驱动整个 GW100 |
| 职责 | 启动与初始化、LPDDR 训练、HDI 命令处理、编程 HDMA、启动各 Tile 的 kernel、跨 Tile 编排、错误处理 | 加载固件、初始化 HDI、管理上下文、提交命令、处理完成和中断 |
| 定义 | `spec-mgmt-tile`、`spec-hdi`（设备一侧） | `spec-pcie`、`spec-hdi`（host 一侧） |
| 本阶段的形态 | 启动 ROM 加固件镜像，裸机 C 程序 | `baremetal/lib/` 中的裸机驱动库，供裸机测试调用 |

两者只通过 BAR 寄存器、doorbell、host 内存中的队列和 MSI-X 交互。

此外还有第三类程序：**kernel**，运行在每个 Compute Tile 的 RISC-V 控制核上，向本 Tile 的矩阵引擎、向量引擎和 DMA 发命令，算子就是用 kernel 实现的。kernel 随程序由驱动经 LOAD_PROGRAM 加载，由管理 Tile 固件启动。

---

## 关键决策

| # | 决策 | 理由 | 何时复审 |
|---|---|---|---|
| D-1 | 固件是真实的 RISC-V 程序，运行在 gem5 的 RISC-V 核上 | 固件代码以后会直接用到硅片上；在模型中运行真实固件，能同时验证规格、硬件模型和固件 | 第 1 天的可行性验证不通过时，见"风险与退路" |
| D-2 | 驱动是 ARM host 上的裸机驱动库 | 驱动的逻辑与 Linux 驱动相同，但不需要先搭 Linux 环境；Linux 驱动以后在此基础上移植 | 端到端链路打通之后 |
| D-3 | 首批算子：INT8 GEMM（累加到 INT32）和向量加，形状取自 Qwen3-0.6B 的一层 | 整数结果按位确定，容易与参考结果比较；数值规格 `spec-numerics` 尚未编写，先不涉及浮点 | 写 `spec-numerics` 时 |
| D-4 | **混合式控制**（Architecture.md 的 D1，已确认）：管理 Tile 负责 host 命令、kernel 启动和跨 Tile 编排；每个 Compute Tile 带一个 RISC-V 控制核，负责本地发令；引擎是命令驱动的协处理器 | 现有规格的"管理 Tile 集中派发张量级指令"（`spec-arch` §2.3）要随之修改，并新增 ADR | 已确认 |
| D-6 | 目标模型 **Qwen3-0.6B**，替换 Architecture.md 的 D3（Qwen2.5-7B） | 用户确认；新增 ADR，结构参数以官方 `config.json` 为准 | 已确认 |
| D-7 | 设备内存沿用规格中的 LPDDR5X（LPDDR5 家族，具体速率不限）；LPDDR5X 训练允许降级：寄存器接口和训练步骤真实，物理过程简化 | 用户确认 | 已确认 |
| D-5 | NoC 先用"固定延迟、队列有限"的理想化互连代替 | `spec-noc` 尚未编写 | 写 `spec-noc` 时 |

---

## 仿真系统的组成

```
gem5（同时编译 ARM 与 RISC-V 两个 ISA）
├── host 系统（ARM，现有的 arm_vexpress 板卡）
│   ├── ARM CPU：运行裸机测试和驱动库
│   └── PCIe 总线上的 GW100 设备
└── GW100 设备
    ├── HIF：PCIe 子系统（已有）、HDMA、ML 的 HIF 侧适配器
    ├── 管理 Tile：RISC-V 核、启动 ROM、2 MiB SRAM、CLINT/PLIC、doorbell 信箱、事件寄存器、ML 的管理侧适配器
    ├── 理想化互连（代替 NoC，第一阶段）
    ├── 设备内存（第一阶段暂用 SimpleMemory）
    └── Compute Tile：RISC-V 控制核、SRAM、DMA、矩阵引擎、向量引擎、同步计数器
```

### 软件的目录（新增）

| 目录 | 内容 |
|---|---|
| `firmware/rom/` | 启动 ROM：初始化 CSR、等 SRAM 清零和 ML 建立、开启动窗口、校验镜像、跳转 |
| `firmware/fw/` | 固件：初始化、事件循环、HDI 命令处理、HDMA 编程、指令派发 |
| `firmware/common/` | ROM、固件与 kernel 共用的寄存器定义和小工具 |
| `kernels/` | 运行在 Tile 控制核上的 kernel：INT8 GEMM、向量加 |
| `tools/fwpack.py` | 生成固件镜像：64 B 镜像头、SHA-256 摘要（`spec-mgmt-tile` §8.2） |
| `baremetal/lib/` | 扩展驱动库：加载固件、HDI 初始化、提交命令、等待完成 |

构建：`make` 新增固件的目标，用 `riscv64-elf-gcc` 编译 ROM 和固件，再由 `tools/fwpack.py` 打包镜像。裸机测试把镜像链接进 host 程序，经 BAR2 写入设备。

---

## 7 天计划

### 第 1 天：多 ISA 可行性验证，搭起管理 Tile 和 ML

- **上午（关口）**：构建同时包含 ARM 和 RISC-V 的 gem5，让 ARM host 系统与一个 RISC-V 核在同一个仿真中运行：RISC-V 核从 ROM 取指，打印一行信息，ARM 端的测试照常通过。**中午之前不通过，就按"风险与退路"切换方案。**
- 把 FLR 的电平加读回方案写进 `spec-pcie` §13.2 和 `spec-mgmt-tile` §9，提交 `spec-mgmt-link` 及相关修改。
- 建模管理 Tile 的骨架：启动 ROM、SRAM（有限 bank、ECC 暂不建模）、CLINT/PLIC（复用 gem5 的 RISC-V 设备）、doorbell 信箱、事件寄存器。
- 建模 ML：两端适配器、四个有限 FIFO、tag、屏障事件、链路状态（`spec-mgmt-link`）。

**完成标准**：ML 的 GTest 通过（顺序、流控、tag、屏障、故障）；RISC-V 核能运行 ROM 中的 hello；现有 12 个裸机测试仍然通过。

### 第 2 天：上电初始化打通

- 启动 ROM（真实 RISC-V 代码）：STAGE 0 → 1 → 2，等待 `FW_START`，校验镜像头和 SHA-256，失败时回到 STAGE 2，成功时跳转（`spec-mgmt-tile` §8）。
- HIF：BAR2 启动窗口拆成 `BOOT_WR` 经 ML 写入 SRAM，提交计数与 `FW_START` 门控，启动通路故障锁存（`spec-pcie` §7.2）。
- 最小固件：设置 PMP 和栈，写 STAGE = 4，置位 `FW_READY`。
- `tools/fwpack.py`；驱动库新增 `gw100_load_firmware()`，实现 `spec-pcie` §6.4 的加载流程。

**完成标准**：裸机测试 `boot_ok`、`boot_bad_digest`、`boot_bad_size`、`boot_flr` 通过；理想化 #11（上电即 STAGE 4）消除。

### 第 3 天：固件框架、HDMA 与中断

- 固件框架：中断入口、PLIC 和 CLINT 驱动、事件循环，以及经 ML 读写 HIF 寄存器的访问层。
- HDMA：8 个通道，描述符环在管理 Tile SRAM 中经 ML 访问，H2D 和 D2H 两个方向，缓冲有限；描述符回写是 non-posted 写（`spec-host-dma`）。
- MSI-X 表与 PBA；固件经 HIF 的中断请求寄存器向 host 发中断。

**完成标准**：裸机测试 `hdma_h2d`、`hdma_d2h`、`hdma_err` 通过（由固件编程 HDMA）；MSI-X 能投递到 host。

### 第 4 天：驱动逻辑打通

- 固件实现 HDI：控制器使能与复位、管理队列（IDENTIFY、CREATE_CONTEXT、SET_CTX_REGION）、上下文队列、COPY_H2D 和 COPY_D2H、完成记录、MSI-X（`spec-hdi`）。
- 驱动库：`hdi_init()`、管理命令、提交命令、轮询或中断等待完成。
- **决策关口**：确认 D-4（Architecture.md 的 D1 – D3）。

**完成标准**：裸机测试 `hdi_init`、`hdi_identify`、`hdi_copy` 通过：host 数据经 HDI 写入设备内存，再读回后一致。

### 第 5 天：计算侧的最小规格与 Compute Tile 模型

- 按 D-4 修改 `spec-arch` 的控制方式，写 ADR；写出 `spec-compute-tile`（含控制核）、`spec-isa`（控制核的 RISC-V 程序与引擎命令格式）、`spec-exe-format`（kernel 启动描述符与控制核程序）的 v0.1 骨架，只覆盖本阶段要用的部分。请 Codex 做一轮快速审查。
- 建模 Compute Tile：RISC-V 控制核、2 MiB SRAM、DMA、向量引擎、矩阵引擎、同步计数器；接上理想化互连和设备内存。

**完成标准**：在单个 Tile 上运行一个 RISC-V kernel，向引擎发命令，SRAM 中的结果正确。

### 第 6 天：算子跑通

- 固件实现 LOAD_PROGRAM 和 LAUNCH：读取程序，经 NoC 向各 Tile 下发 kernel 启动描述符，用同步计数器等待完成，写完成记录（`spec-hdi` §10）。
- 用 RISC-V C 编写两个 kernel：向量加、单 Tile 的 INT8 GEMM；时间允许时再做 4 个 Tile 分块的 GEMM。

**完成标准**：裸机测试 `op_vadd`、`op_gemm` 通过：驱动搬入数据、加载程序、启动、搬回结果，与 host 参考实现按位一致。

### 第 7 天：集成、文档与缓冲

- 端到端测试 `e2e_gemm`：从上电一路跑到算子结果比对。
- 更新理想化清单、`gem5-ext/README.md`、`baremetal/README.md`，补写固件的构建与调试说明（含 RISC-V 核的远程 GDB）。
- 按 scope 拆分提交，准备 PR。
- 这一天同时是前 6 天的缓冲。

**完成标准**：`make test` 和全部单元测试通过；文档与模型一致。

---

## 优先级：落后时砍什么

| 必须保住 | 可以推迟 |
|---|---|
| 第 1 天的多 ISA 关口 | ECC、SRAM bank 冲突 |
| 第 2 天的上电初始化 | 启动期间 FLR 的测试 |
| 第 4 天的 HDI 数据往返 | MSI-X（改为轮询完成队列）、INTx |
| 第 6 天的单 Tile GEMM | 多 Tile GEMM |

---

## 风险与退路

| 风险 | 影响 | 应对 |
|---|---|---|
| ARM host 与 RISC-V 核无法在同一仿真中运行（多 ISA 构建失败，或两个系统无法共享事件队列和端口） | 整个计划的基础 | 第 1 天中午前决定：① 首选一个 gem5 进程、两个 System（ARM host 与 GW100 管理系统）；② 不行就在设备内嵌一个小型 RV64IMAC 指令集模拟器，运行同一份固件；③ 再不行就暂时用 C++ 实现固件行为，登记为理想化，固件代码照常编写以备替换 |
| 固件工作量超出预期 | 第 3、4 天延期 | 固件只实现本阶段用到的命令；HDI 的错误路径和 GET_LOG 等推迟 |
| 第 5 天既写规格又建模型，最紧 | 算子推迟到第 7 天 | 规格只写本阶段用到的指令；向量加先行，GEMM 随后 |
| 混合式控制让第 5、6 天的工作量增加（每个 Tile 多一个 RISC-V 核和 kernel） | 算子推迟到第 7 天之后 | 第一阶段先做单 Tile；控制核复用 gem5 的 RISC-V CPU 模型 |
| 理想化积累过多 | 模型与硬件偏离 | 每一项都登记进理想化清单，并写明消除计划 |

---

## 每天的固定流程

1. 改了 gem5-ext 的 C++ 或 SimObject 参数：先重新构建 gem5。
2. 当天结束：`make test` 与全部 GTest 通过，并汇报结果。
3. 新做的理想化当天登记。
4. 提交需另行确认；不在 `main` 上开发。

---

## 本阶段之后

- 用 DRAM 时序模型代替 SimpleMemory，用 `spec-noc` 定义的 Mesh 代替理想化互连。
- 补齐 `spec-numerics`，加入 FP16/BF16 算子和 LLM 常用算子（RMSNorm、Softmax、注意力）。
- 将驱动库移植成 Linux 驱动，搭建全栈 E2E 测试。
- 重建架构参数和 ISA 的单一真源（`tools/isagen/`）。
