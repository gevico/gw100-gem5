# Goal：GW100 第一版端到端链路，并消除链路上的理想化

## 目标

在 gem5 中实现从上电到算子执行的端到端链路，链路上的每个部件都按规格建模，不依赖理想化：

上电 → 启动 ROM → host 驱动经 BAR2 加载固件 → 固件初始化（含 LPDDR5X 训练流程）→ 驱动初始化 HDI、创建上下文 → HDMA 搬入数据 → 驱动加载并启动程序 → 管理 Tile 固件经 NoC 启动各 Compute Tile 上的 kernel → Tile 控制核向矩阵引擎、向量引擎和 DMA 发命令，执行 INT8 GEMM 和向量加 → HDMA 搬回结果 → 与 host 参考实现按位一致。

背景和逐日计划见 `Roadmap.md`；本文件是这个 Goal 的完整要求，两者冲突时以本文件为准。

## 已确定的决策（不需要再问）

| 决策 | 内容 |
|---|---|
| 控制方式 | **混合式**（Architecture.md 的 D1，已确认）：管理 Tile 的全局 CPU 负责 host 命令、kernel 启动和跨 Tile 编排；每个 Compute Tile 带一个小 RISC-V 控制核，负责本地发令；矩阵引擎、向量引擎、DMA 是命令驱动的协处理器。现有规格中"管理 Tile 集中派发张量级指令"和"指令通道"（`spec-arch` §2.3、§4 等）按此修改，并新增 ADR 记录 |
| 固件 | 管理 Tile 和每个 Tile 控制核运行的都是**真实的 RISC-V 程序**，由 `riscv64-elf-gcc` 编译，运行在 gem5 模拟的 RISC-V 核上 |
| 驱动 | ARM host 上的裸机驱动库（`baremetal/lib/`），供裸机测试调用 |
| 设备内存 | LPDDR5 家族，具体速率不限：沿用现有规格的 LPDDR5X（32 GiB、8 通道）。JEDEC 参数回原始资料核对；资料不在 `reference/` 中时，标"待核对"并列入汇报 |
| 目标模型 | **Qwen3-0.6B**，替换 Architecture.md 的 D3（Qwen2.5-7B）；新增 ADR 记录。结构参数以官方 `config.json` 为准，写入规格前核对 |
| 算子 | INT8 GEMM（累加到 INT32）和向量加，形状取自 Qwen3-0.6B 的一层（例如 q_proj、gate_proj），具体形状核对 `config.json` 后确定 |
| LPDDR5X 训练 | 允许降级：寄存器接口和固件的训练步骤必须真实，训练的物理过程可以简化。这一项登记进理想化清单，视为**已批准** |
| 提交 | 全程在当前分支 `feat/gw100-pcie-endpoint` 上工作并自行提交，不切换、不新建分支，不推送、不开 PR |

## 完成的定义（全部满足才算完成）

1. `make test` 全部通过，其中包括端到端测试 `e2e_gemm`；全部 GTest 单元测试通过。
2. 管理 Tile 固件和 Tile kernel 都是真实的 RISC-V 程序，在 gem5 的 RISC-V 核上运行。
3. `gem5-ext/README.md` 的理想化清单中，端到端链路经过的部件没有未消除的条目；例外只有上面已批准的一项，以及在"待批准的理想化"中列出、并得到我明确批准的条目。
4. 链路上每个部件都有对应的规格，经 Codex 交叉审查，最后一轮没有"必须改"。
5. 文档与模型一致：各份规格、ADR、`gem5-ext/README.md`、`baremetal/README.md`，以及固件和 kernel 的构建与调试说明。

## 什么算理想化（不得引入；已有的要消除）

- 无界的缓冲或队列；不受流控约束的发送方。
- 规格没有给出依据的零延迟或固定延迟；用功能访问（functional access）绕过时序端口。
- 用 SimpleMemory 代替 DRAM 控制器和 LPDDR5X 时序；用交叉开关或固定延迟互连代替 Mesh NoC。
- 用 C++ 行为模型代替应由固件或 kernel 完成的工作。
- 测试、固件或 kernel 经后门直接读写模型状态；只为让测试通过而存在的特殊路径。
- 规格要求、但模型没有实现的检查、错误路径或复位行为。

确实无法在本 Goal 内消除的，按以下方式处理，绝不悄悄保留：登记进理想化清单（位置、现状、偏乐观还是偏悲观、影响、消除计划），并在汇报的"待批准的理想化"中单独列出，等我批准。

## 工作顺序与关口

按顺序推进，前一个关口不通过，不进入下一个。

- **M0 准备与可行性**
  - 在当前分支 `feat/gw100-pcie-endpoint` 上，把工作区中尚未提交的规格修改（`spec-mgmt-link` 及其连带修改）、`Roadmap.md`、`Goal.md` 作为第一批提交，按 scope 拆分。
  - 构建同时包含 ARM 和 RISC-V 的 gem5，在同一个进程中运行 ARM host 和多个 RISC-V 核（1 个管理核，加上 Compute Tile 控制核）。不可行时停下来，列出备选方案和代价，等我决定。
- **M1 管理 Tile 与 ML**：先把 FLR 的电平加读回方案写进 `spec-pcie` §13.2 和 `spec-mgmt-tile` §9（去掉 `FLR_SEQ`、`LAST_ACK`、LIMIT，FLR 进行中时丢弃 doorbell），再按 `spec-mgmt-tile`、`spec-mgmt-link` 建模管理 Tile 和 ML。
- **M2 上电初始化**：启动 ROM、启动窗口、提交计数与 `FW_START` 门控；`tools/fwpack.py`；驱动加载固件。
- **M3 固件框架、HDMA、MSI-X/INTx**。
- **M4 HDI**：管理队列、上下文、COPY、完成与中断；驱动库。

  到这里是端到端的骨架。NoC、DRAM、Compute Tile 可以暂时使用登记过的理想化，但必须先让 `hdi_copy` 测试通过，才能进入 M5。

- **M5 规格**
  - 按 D1 修改 `spec-arch` 和受影响的规格，新增 ADR：混合式控制、目标模型改为 Qwen3-0.6B。
  - 新写 `spec-noc`、`spec-dram`、`spec-compute-tile`（含控制核）、`spec-isa`（分层：控制核的 RISC-V 程序与引擎命令格式）、`spec-exe-format`（kernel 启动描述符与控制核程序）、`spec-tile-sync`、`spec-numerics`（只覆盖 INT8）。
  - 每份都交叉审查到没有"必须改"。Compute Tile 的 SRAM 容量等参数，按 Qwen3-0.6B 的映射重新推算。
- **M6 NoC**：按 `spec-noc` 实现 6 × 4 Mesh、XY 路由、有限缓冲与信用流控，替换理想化互连。
- **M7 设备内存**：按 `spec-dram` 实现 Memory Tile、DRAM 控制器和 LPDDR5X 时序，替换 SimpleMemory；固件实现训练流程（按上面已批准的降级）。
- **M8 Compute Tile 与算子**
  - 建模 RISC-V 控制核、SRAM、DMA、矩阵引擎、向量引擎、同步计数器。
  - 管理 Tile 固件实现 LOAD_PROGRAM、LAUNCH 和 kernel 启动。
  - 用 RISC-V C 编写 INT8 GEMM 和向量加的 kernel。
- **M9 收尾**：端到端测试 `e2e_gemm`、文档、清理理想化清单。

## 规格先行

- 建模任何部件之前，它的规格必须已存在并通过交叉审查；模型与规格冲突时，先改规格。
- 写进规格的数字要回 PDF 原页核对，引用写"代号 p.N"，自己推算的标"推算"。
- 遵守 `docs/README.md`：每项定义只在一份规格中给出，规格之间相互引用；新增规格或 ADR 先在 `tools/git-hooks/scopes.txt` 登记。
- `Architecture.md` 是本地的调研记录，只作参考；它与本文件冲突时以本文件为准，定下的内容以规格和 ADR 为准。

## 仓库规则（必须遵守）

- 遵守 `CLAUDE.md` 和 `CONTRIBUTING.md` 的全部规则。
- 在当前分支 `feat/gw100-pcie-endpoint` 上开发，不切换、不新建分支；按 CONTRIBUTING §3 的格式提交：每个提交只做一件事、最多两个 scope，每个提交都能构建且测试全部通过。不得使用 `--no-verify`。
- 不签署 DCO，不写 Co-Authored-By，不写"Generated with Claude Code"。
- 不推送、不开 PR，除非我明确要求。
- 改了 gem5-ext 的 C++ 或 SimObject 参数，先重新构建 gem5。
- 裸机测试放 `baremetal/tests/`，GTest 放 `gem5-ext/tests/unit/`，新源文件用 `tools/templates/` 的文件头。
- `reference/hailo/hailort-drivers/` 是 GPL-2.0 代码，只能阅读，不得复制。

## 验证纪律

- 不得为了让测试通过而削弱、跳过或删除测试，也不得修改期望值去迁就模型。
- 测试失败时先找根因再修；修不好就如实汇报，附上失败输出。
- 每个关口结束时运行全部测试；重构类改动用调试输出的前后对比证明行为不变。

## 必须停下来问我的情况

- 任何改变已有规格或 ADR 中既定决策的修改（上面"已确定的决策"列出的除外）。
- 规格需要一个无法从资料或推算得出的参数值。
- 需要删除或重写已有的大段规格或模型。
- 任何一项理想化需要保留（已批准的 LPDDR5X 训练降级除外）。
- 连续两次尝试仍无法通过某个关口。

停下来时，写清问题、可选方案、各自的代价和你的建议，然后等我回复；等待期间可以继续做不依赖这个决策的工作。

## 汇报

每过一个关口汇报一次，只写：

- 完成了什么；
- 测试结果（通过数 / 总数，失败的附输出）；
- 新增或消除的理想化；
- 待批准的理想化；
- 等我决定的问题。
