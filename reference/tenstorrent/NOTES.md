# 阅读笔记：Tenstorrent（Grayskull / Wormhole / Blackhole）

| 项目 | 内容 |
|---|---|
| 相关性 | ★★★ 公开资料里**唯一细到寄存器级**的同类架构：多种 Tile 在同一张 2D 网格、NoC、PCIe Tile、L1 原子操作都有完整文档 |
| 对应文档 | SPEC-100 / 110 / 120 / 200 / 210 / 240 / 250 / 300 / 310 / 350，DOC-002 |

## 资料与引用代号

讲稿和论文写作"代号 p.页码"，页码对应 PDF 页，也对应同名 `.md` 里的 `<!-- p.N -->` 标记。官方文档本身就是 Markdown，按文件路径引用。

| 代号 | 资料 |
|---|---|
| HC32 | [2020-hotchips32-compute-substrate/](2020-hotchips32-compute-substrate/)：Grayskull 与 Wormhole（Hot Chips 2020） |
| HC36 | [2024-hotchips36-blackhole-metalium/](2024-hotchips36-blackhole-metalium/)：Blackhole 与 TT-Metalium（Hot Chips 2024） |
| ISA | [tt-isa-documentation/](tt-isa-documentation/)：官方 ISA 文档，下文路径都相对于 `WormholeB0/` |
| TR | [tt-metal-tech-reports/](tt-metal-tech-reports/)：官方技术报告选集 |
| 3RD | [2025-arxiv-riscv-matmul-assessment/](2025-arxiv-riscv-matmul-assessment/)：第三方对 Grayskull 矩阵乘的评测 |
| EPCC | [2025-hpcasia-epcc-intro/](2025-hpcasia-epcc-intro/)：入门教程讲稿 |

## 三代芯片

| 参数 | Grayskull（2020） | Wormhole（2021） | Blackhole（2024） | 出处 |
|---|---|---|---|---|
| 工艺 | 12 nm | 12 nm | 6 nm | HC36 p.3 |
| Tensix 数 | 120 | 80（按产品开放 64 / 72 / 80） | 140 | HC36 p.3；ISA `README.md` |
| 算力 | 276 TOPS（FP8）；HC32 写作 368 TOPS / 92 TFLOPS | 328 TOPS（FP8） | 745 TFLOPS（8-bit）/ 372 TFLOPS（16-bit） | HC36 p.3、p.5；HC32 p.27 |
| 片上 SRAM | 120 MB | 每 Tensix 1464 KiB | 全片 241 MB；每 Tensix 1536 KiB | HC32 p.27；HC36 p.5；ISA `TensixTile/L1.md` |
| 片外内存 | 8 路 LPDDR4，约 100 GB/s | GDDR6，12 GiB，336 GB/s | GDDR6，32 GB，512 GB/s | HC32 p.27；HC36 p.3、p.5 |
| Host 接口 | PCIe Gen4 x16 | PCIe Gen4 x16 | PCIe Gen5 x16（64 GB/s） | HC32 p.27；HC36 p.5 |
| 功耗 | 65 W | — | — | HC32 p.27 |
| 管理与通用 CPU | 4 核 ARC，可跑 Linux | ARC 管理 Tile | 16 个 RV64 大核（4 簇 × 4），可跑 Linux | HC32 p.27；HC36 p.6 |

两份讲稿给出的 Grayskull 算力不一致，口径不明。

## Tensix Tile（ISA `TensixTile/README.md`、`L1.md`；HC36 p.6–16）

- 5 个小 RISC-V 核（RV32IM），各有 4 KB 指令 cache 和 8 KB 数据暂存器。典型分工是 2 个管 NoC 数据搬运，3 个驱动 Tensix 协处理器。
- Tensix 协处理器：2 个 Unpacker（L1 → 协处理器）、矩阵单元 FPU、向量单元 SFPU（32 宽 SIMD，32-bit lane）、标量单元 ThCon、4 个 Packer（协处理器 → L1）。
- L1：Wormhole 上 1464 KiB，分 16 个 bank，每个 bank 每周期读或写 128 bit，16 个访问端口，有冲突时轮询仲裁。
  - 不足 128 bit 的窄写是读-改-写，要占 5 个周期。
  - 支持原子操作：无条件原子（如加一）和条件原子（如比较后加一，用于 FIFO 元数据）。
  - 支持近存累加（FP32、INT32、FP16、BF16），由 Packer 发起。
- **编程模型**（HC36 p.11–12）：每个 Tensix 跑 3 个 C++ kernel，1 个计算 kernel 被自动拆成 3 个 RISC-V 线程，另有 2 个数据搬运 kernel。kernel 之间用 Circular Buffer（带硬件流控的 SRAM 对象）同步，另有原子信号量。

## 矩阵与数据格式（TR `matrix_engine/`、`data_formats/`；HC36 p.16）

- 矩阵引擎每周期做一次 8×16 × 16×16 的乘法（4096 次乘加），1 GHz 时 4 TFLOPS。
- **输入少于 8 行时，硬件照样按 8 行算，有效吞吐按比例下降**：1 行输入只剩 1/8（0.5 TFLOPS）。这正是 batch=1 decode 时 GEMV 的情形，是"大阵列做 GEMV 效率低"的厂商实证。
- 用"保真度"换精度：LoFi 4 TFLOPS，HiFi2 2 TFLOPS，HiFi3 1.33 TFLOPS，HiFi4 1 TFLOPS。也就是用多遍低精度乘法凑出高精度结果。
- 矩阵引擎支持的格式：块浮点 BFP2 / BFP4 / BFP8、FP8、BF16、TF32（累加 FP32），INT8（累加 INT32）；向量引擎支持 FP32、INT16、INT32。
- 块浮点转换采用就近舍入、偶数优先；共享指数时，先把尾数按共享指数对齐再舍入。

## NoC（ISA `NoC/`）

| 主题 | 要点 | 文件 |
|---|---|---|
| 拓扑 | 两张物理上独立、方向相反的 NoC，都是 2D torus | `NoC/README.md` |
| 包格式 | flit 256 bit；1 个头 flit 加最多 256 个数据 flit（8 KB） | `NoC/README.md` |
| 请求类型 | 读；写（≤16 B 可带任意字节使能，可用 32 位立即数，可选不要应答，可广播到一个矩形内的 Tensix）；原子（只对 L1） | `NoC/README.md` |
| 虚通道 | 4 bit：1 bit dateline（torus 防死锁）、2 bit 消息类（两类单播请求、广播请求、响应）、1 bit buddy（随拥塞可变） | `NoC/README.md` |
| 死锁 | 常用事务由硬件保证不死锁；多包事务、不预留路径的广播、非零仲裁优先级，需要软件负责 | `NoC/README.md` |
| 延迟 | 每经过一个路由器 9 个周期；NIU 到路由器约 5 个周期 | `NoC/README.md` |
| 顺序 | 默认弱序；同源同目的、同虚通道的包不重排；接收端有若干条保序规则（如先写后读）；写 L1 后写 MMIO 不保序；要更强的顺序只能等应答后再发下一条 | `NoC/Ordering.md` |
| 原子操作 | 加一（可指定位宽）、比较交换等，只能作用于 L1；结果可回写发起方 | `NoC/Atomics.md` |
| 计数器 | 每个 NIU 有一组计数器，记录已发、已收、未完成的请求（按事务 ID 分），软件靠它判断传输是否完成 | `NoC/Counters.md` |
| 坐标转换 | NIU 可以做坐标转换：两张 NoC 用统一坐标；同类 Tile 坐标连续；**被屏蔽的 Tensix 行重新编号到最后**（良率冗余） | `NoC/Coordinates.md` |

## PCIe Tile（ISA `PCIExpressTile/`）

- BAR0 512 MiB，BAR2 1 MiB，BAR4 32 MiB；入站 iATU 把 BAR4 设成 BAR0 最高 32 MiB 的别名（`README.md`）。
- **186 个可配置的 TLB 窗口**，占 BAR0 低 496 MiB：156 个 1 MiB、10 个 2 MiB、20 个 16 MiB（`TLBs.md`）。
- 每个窗口 64 bit 配置：目标地址高位、目标 Tile 坐标（或多播矩形的起止坐标）、走哪张 NoC、是否多播、顺序模式（默认 / 严格 AXI / posted 写）、虚通道选项（`TLBs.md`）。
- host 发出的 PCIe 事务先转成 AXI，再转成 NoC 事务；设备发往 host 的超过 128 B 的请求，通常会被拆成多个 PCIe 事务（`NoC/README.md`）。

## 工程经验（TR）

| 主题 | 要点 | 文件 |
|---|---|---|
| DRAM 带宽 | 实测 DRAM 带宽利用率**超过 92%**。做法：每个 bank 配一个读取者，只读自己的 bank；读取者放在离 bank 一跳的位置，避免 NoC 拥塞；按事务 ID 分块等待，让读请求流水起来；带宽受限的算子不用交织布局，改为按 bank 分片 | `Saturating_DRAM_bandwidth/` |
| LLM decode | 矩阵乘受 DRAM 带宽限制，采用"DRAM 分片矩阵乘"：权重按 bank 切分，激活值多播；残差流常驻 L1 并分片；KV cache 分页；decode 专用的 flash attention | `LLMs/llms.md` |
| 性能计数器 | 硬件性能计数器的使用方法 | `PerfCounters/` |
| PCIe 带宽 | 分 host API 层和 kernel 层两级测量；host 层带宽包含派发开销 | `PCIe_bandwidth/` |

## 片上数据搬运带宽（HC36 p.13–14，Blackhole）

| 访问模式 | 带宽 |
|---|---|
| 本地 / 分片 | 94 TB/s |
| 相邻 Tile | 47 TB/s |
| 行 / 列 / 全网格多播 | 24 TB/s |
| 3 跳 gather / scatter | 16 TB/s |
| 10 跳 gather / scatter | 5 TB/s |
| DRAM | 512 GB/s |

单条 NoC 链路 83 GB/s（64 B 宽）；每个 Tensix 到 NoC 的 I/O 带宽 665 GB/s。

## 早期设计（HC32 p.15–24）

- Grayskull 是 120 个独立核组成的 2D 双向 torus，每个核执行自己的程序。
- 当时的模型是"包管理器 + 包计算引擎"：编译器把张量切成带包头的 mini-tensor，不用指针，全部用包 ID 表达；包管理器负责张量变换、数据搬运、多核同步和路由；运行时做动态内存分配。
- 强调条件计算和 16×16 块稀疏，需要硬件能吃到块稀疏的全部加速（p.13）。

## 对我们的启示

| 我们的设计问题 | 借鉴点 | 对应文档 |
|---|---|---|
| Tile 种类与布局 | 计算、DRAM、以太网、PCIe、管理 Tile 都在同一张网格上；Memory Tile 的位置要照顾"读取者离 bank 一跳" | SPEC-100 / 300 |
| 良率冗余 | 用坐标转换把被屏蔽的 Tile 行重新编号到最后，软件看到的坐标连续 | SPEC-100 / 120 |
| NoC 包格式与虚通道 | 256-bit flit，消息类绑定虚通道，矩形多播，原子操作，可选的顺序强度 | SPEC-120 |
| 传输完成判断 | NIU 计数器按事务 ID 统计未完成请求 | SPEC-120 / 250 |
| Host 地址窗口 | 可配置的 TLB 窗口：映射到 Tile 坐标或多播矩形，带顺序模式 | SPEC-110 / 310 |
| L1 设计 | 分 bank、多端口；窄写代价高；原子操作和近存累加 | SPEC-200 |
| 矩阵引擎 | 最小输入块的行数直接决定 GEMV 效率；我们的阵列形状要针对 batch=1 来定 | SPEC-210 |
| 数据格式 | 块浮点作为权重压缩格式；用保真度换精度的做法可以参考 | SPEC-240 |
| decode 映射 | 权重按 DRAM bank 分片、就近读取，激活值多播，残差常驻片上 | MAN-020，DOC-002 |
| DRAM 效率 | 92% 的利用率说明，只要映射得当，带宽利用率可以远高于 M100 推算的 67% | DOC-002 |

## 存疑与待核对

- Blackhole 的 ISA 文档也在 `tt-isa-documentation/BlackholeA0/` 里，和 Wormhole 不完全相同；上面引用的都是 Wormhole 的文档。
- "超过 92%"是专门优化过的带宽测试结果，实际模型里能达到多少，要看 LLM 报告里的性能数据。
