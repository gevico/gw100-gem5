# 阅读笔记：AMD XDNA / XDNA2

| 项目 | 内容 |
|---|---|
| 相关性 | ★★★（host 接口与固件）／★★（计算阵列）。驱动文档是我们 Host-Device 接口最完整的公开范例 |
| 对应文档 | SPEC-100 / 250 / 310 / 320 / 330 / 400，DOC-001 |

## 资料与引用代号

表中"出处"一列写作"代号 p.页码"，页码对应 PDF 页，也对应同名 `.md` 里的 `<!-- p.N -->` 标记。

| 代号 | 资料 | 类型 |
|---|---|---|
| HC35 | [2023-hotchips35-ryzen7040-phoenix/](2023-hotchips35-ryzen7040-phoenix/) | Hot Chips 2023 讲稿，XDNA 只在 p.12 |
| KDOC | [linux-kernel-amdxdna-doc/amdnpu.rst](linux-kernel-amdxdna-doc/amdnpu.rst) | Linux 内核驱动文档，按章节引用 |
| GEMM | [2025-arxiv-ryzen-ai-gemm-generations/](2025-arxiv-ryzen-ai-gemm-generations/) | XDNA 与 XDNA2 的架构对比和 GEMM 实测 |
| BARE | [2025-arxiv-npu-bare-metal-training/](2025-arxiv-npu-bare-metal-training/) | 用底层工具在 XDNA 上做训练 |
| LLM | [2026-mlarchsys-llm-on-xdna2/](2026-mlarchsys-llm-on-xdna2/) | 在 XDNA2 上端到端部署 LLM |
| IRON | [mlir-aie-programming-guide/](mlir-aie-programming-guide/) | 官方编程指南，按章节引用 |

## 关键参数

| 参数 | XDNA（Phoenix / Hawk Point） | XDNA2（Strix Point） | 出处 |
|---|---|---|---|
| 阵列 | 4 行 × 5 列 = 20 个计算 Tile | 4 行 × 8 列 = 32 个计算 Tile | KDOC「AMD XDNA Array」；GEMM p.3 |
| 峰值算力 | 10 TOPS INT8 / 20 TOPS INT4 / 5 TFLOPS BF16（另有 16 TOPS 的 Hawk Point） | 50 TOPS INT8 | HC35 p.12；GEMM p.3 |
| 计算 Tile | VLIW + SIMD 核，独立程序存储，64 KB 数据存储（L1）；Tile 内含 DMA、锁、流接口 | 同左 | HC35 p.12；GEMM p.3 |
| BF16 吞吐 | 每核每周期 128 次 FMA（bf16 输入、fp32 输出），1 GHz 时每核 256 GFLOP/s | — | BARE p.3 |
| 存储 Tile（L2） | 每个 512 KB，一整行，共 2560 KB | 每个 512 KB，共 4096 KB | KDOC「Shared L2 Memory」；GEMM p.3 |
| 数据类型 | int8、int16、bf16；支持 50% 权重稀疏 | 另加块浮点 bfp16（8 个数共享指数） | GEMM p.3；HC35 p.12 |
| 并发 | 最多 6 个工作负载上下文；最多 4 路空间并发流 | 最多 16 个上下文 | KDOC「Resource Solver」；HC35 p.12 |
| 片外带宽 | — | LPDDR5，与 CPU 共享，约 120 GB/s | cross-vendor 评测（见 [../cross-vendor/NOTES.md](../cross-vendor/NOTES.md)） |
| GEMM 实测 | int8 6.76 TOPS，bf16 3.14 TOPS | int8 38.05 TOPS（约为峰值的 76%），bf16 14.71 TOPS | GEMM p.2 |
| LLM 实测 | — | Llama-3.2-1B（BF16）：2048 token prefill 首 token 1.3 s，decode 10.8 tok/s | LLM p.3 |

## 数据搬运与同步（GEMM p.3、p.5–6；IRON section-2）

- 每个 Tile 都有 DMA。计算 Tile 和 Shim Tile 各有 2 个读出通道（MM2S）和 2 个写入通道（S2MM）；存储 Tile 各有 6 个。
- DMA 由一串缓冲区描述符（BD）编程。每个 BD 写明：
  - 搬运长度和地址；
  - 多维寻址参数：计算 Tile 和 Shim Tile 支持 3 维，存储 Tile 支持 4 维；
  - **传输前要获取、传输后要释放的硬件锁**。
- 每个 Shim Tile 最多 16 个 BD。GEMM 尺寸大时 BD 不够用，需要中途重新配置（GEMM p.6）。**这提示我们描述符数量的上限要按 LLM 的实际形状来定。**
- Tile 之间通过可配置的流交换网络传数据，可走电路交换，也可走包交换；相邻计算 Tile 之间有 cascade 接口，用来传部分累加和，代价是平均约 7% 的性能下降（GEMM p.2）。
- 运行时由片上的命令处理器负责：配置 kernel、交换网络和 DMA，以及控制 Shim Tile 与 DRAM 之间的搬运（GEMM p.3）。
- 编程层面，IRON 用 ObjectFifo 把"DMA 加锁"封装成生产者-消费者队列（IRON section-2a、2b）；数据布局变换由 DMA 的多维寻址完成（section-2c）。

## 固件与 host 接口（KDOC）

| 主题 | 内容 | KDOC 章节 |
|---|---|---|
| 微控制器固件 | 负责命令处理、阵列分区和配置、上下文管理、执行编排 | Microcontroller |
| 执行环境 | MERT：全局唯一的特权环境，处理驱动的管理命令；ERT：每个上下文一个，非特权，执行用户的 ctrlcode；二者由微控制器的 MMU 隔离 | Microcontroller、Process Isolation Hardware |
| mailbox | 特权通道绑定一个 mailbox；每个用户上下文各有一个专用 mailbox | Mailboxes |
| BAR | 按功能分为 PSP、SMU、SRAM（mailbox 环形缓冲区）、Mailbox（头尾指针和中断状态寄存器）、公共寄存器；不同型号会合并到不同的物理 BAR | PCIe EP |
| 中断 | 每个 ERT 一个 MSI-X 向量，MERT 一个 | PCIe EP |
| 隔离 | 阵列按列切成空间分区，每个分区绑定一个 PASID；DMA 访问 host 内存时由 IOMMU 按 PASID 检查 | Process Isolation Hardware |
| 调度 | 分区可以独占，也可以被多个上下文分时共享，分时共享时固件切换 PASID；驱动里的 Resource Solver 决定分区方案 | Mixed Spatial and Temporal Scheduling |
| 程序格式 | overlay（配置流交换网络、加载各计算 Tile 的 ELF）＋ ctrlcode（由 ERT 执行的操作码序列） | Application Binaries |
| 指令缓冲区 | 每个上下文在 host 内存有 64 MB 缓冲区放 ctrlcode，映射给 ERT 和用户态，受 PASID 保护 | Special Host Buffers |
| 执行流程 | 编译 → 建上下文 → 分配列 → MERT 建 ERT → 用户态写入 ctrlcode → 提交命令缓冲区 → ERT 执行（期间驱动 host 与 L2 之间的 DMA）→ 结束时发 MSI-X | High-level Use Flow |
| 启动 | 驱动经 PSP 加载签过名的固件；在 BAR0 指定位置等待存活信号；挂起恢复后重新加载固件并握手 | Boot Flow |
| 出错 | MERT 暂停出错的上下文并通知驱动；驱动提供缓冲区，MERT 把分区寄存器状态转储进去 | Error Handling |
| 遥测 | L1 中断计数、DMA 计数、深度睡眠计数等 | Telemetry |

## LLM 部署的教训（LLM p.3）

- **派发开销**：每次派发 kernel 都要经过应用、runtime、驱动、固件、硬件多层，开销可能超过 kernel 本身的执行时间。
  - prefill 时，把注意力之后的 8 个 kernel 合成 1 次派发，注意力之前的 6 个合成 1 次，每层从 15 次派发降到 3 次。decode 用 GEMV 版本，做同样的合并。
- **host 端开销**：上下文建立、缓冲区管理、host 与设备之间的数据拷贝、布局转置，在大量 kernel 调用中会累积起来。对策是：
  - 复用上下文；
  - 用零拷贝映射复用每层的缓冲区；
  - 跳过不必要的拷贝；
  - 选择让相邻 kernel 能直接衔接的数据布局，省掉 host 端转置；
  - prefill 只回传最后一个 token 的 logits。
- 效果：prefill 提速 2.2 倍，decode 提速 4 倍。

## 对我们的启示

| 我们的设计问题 | 借鉴点 | 对应文档 |
|---|---|---|
| 固件上下文 | 一个特权管理上下文，加上每个工作负载一个非特权上下文；由 MMU 隔离 | SPEC-330 |
| 命令通道 | 每个上下文独立的 mailbox（环形缓冲区加头尾指针）和 MSI-X 向量 | SPEC-400 / 310 |
| 隔离与分区 | 按 Tile 列或 cluster 切分区，每个分区绑定地址空间 ID，DMA 访问 host 时由 IOMMU 隔离；分区既可独占也可分时共享 | SPEC-100 / 400 |
| 同步写进描述符 | BD 里写明传输前后获取和释放的锁；我们的 DMA 描述符应写明等待和通知的计数器 | SPEC-250 / 320 |
| 描述符容量 | Shim Tile 只有 16 个 BD，大 GEMM 要中途重新配置；我们按 LLM 的实际形状确定描述符数量上限 | SPEC-320 |
| 多维寻址 | DMA 支持 3–4 维寻址，在搬运中完成布局变换 | SPEC-320 |
| 派发开销 | 一次提交覆盖整个 decode 步骤；把"单次提交开销"作为 KPI | SPEC-400，DOC-001 |
| 出错处理 | 暂停出错上下文、转储分区状态、驱动解析 | SPEC-340 |

## 存疑与待核对

- 计算阵列的官方架构手册 AM020 / AM027 没拿到（见 [../README.md](../README.md)），所以 AIE-ML 核的微架构、流交换网络的细节、锁的数量与语义都还没有一手资料。
- HC35 讲稿 p.12 的框图画的是 4×4 阵列，内核文档和论文说 Phoenix 是 4×5。按论文 BARE p.2 的说法，5 列中只有 4 列带 Shim Tile，讲稿可能是简化画法。
