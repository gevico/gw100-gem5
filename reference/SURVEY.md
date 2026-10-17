# 数据流 AI 芯片架构调研

> 写给：架构/gem5 团队，也可供驱动、编译器团队了解背景　·　2026-10-17
> 第一批：Groq、Tenstorrent、理想 M100、Google TPU（§3.1–3.4）。第二批：端侧 NPU 竞品 Hailo-10H、Axelera Metis/Europa、AMD XDNA（§3.5–3.7）。
> 原始资料见 [README.md](README.md)；每家的详细参数、出处页码见各厂商目录下的 `NOTES.md`；设计精髓、经验教训和我们的设计原则见 [LESSONS.md](LESSONS.md)。凡标"推算"的数字是我们根据论文数据自己计算的，不是原文结论。

## 0. 结论先行

1. **四家都没有给计算单元配 cache。** 片上存储全部是软件管理的 scratchpad，数据搬运由软件显式发起。我们的 SPEC-110、SPEC-200 应按 scratchpad 加显式 DMA 来设计，不做缓存一致性。
2. **调度的静态程度是这几家最大的差别**（§1）。和我们场景最接近的是**理想 M100**：端侧部署、LPDDR5X、跑 LLM、单芯片。Groq 的全静态调度要求权重全部放在片上 SRAM，和我们"权重放在 DRAM"的前提冲突，不适用。
3. **同步机制普遍采用"计数器式生产者-消费者"**：M100 叫同步计数器，Tenstorrent 叫 Circular Buffer 加信号量。SPEC-250 可以直接按这个模式定义。
4. **2D Mesh 加多播是共识。** M100 另外加了一条确定性的广播环，专门把 DRAM 读出来的权重广播给所有计算单元。这个方案和 Mesh 多播哪个更好，是我们 gem5 设计空间探索的头号课题。
5. **decode 受带宽限制，这一点有了实测数据。** M100 跑 LLaMA2-7B（W4A16）decode 每 token 21.34 ms，DRAM 带宽 273 GB/s。推算有效带宽利用率约 67%。按同样效率推到我们的 128-bit LPDDR5X（136 GB/s），约 23 tok/s。详见 §4。
6. **良率冗余必须在架构里预留。** M100 实测只启用 14 个 cluster 中的 12 个；Tenstorrent Wormhole 的 80 个 Tensix 按产品只开放 64、72 或 80 个。所以 SPEC-100 需要定义 Tile 屏蔽和坐标重映射机制。
7. **Host 接口可以直接借鉴 Tenstorrent。** 它的 PCIe Tile 用可配置的 TLB，把 BAR 窗口映射到 NoC 坐标加偏移，还支持矩形广播。这个设计可以直接参考进 SPEC-110 和 SPEC-310。

**第二批（端侧 NPU）新增的结论：**

8. **端侧竞品再次证明：DRAM 带宽决定 LLM 能跑多大。** Hailo-10H 和 Axelera Metis 用的是 LPDDR4/4x，只够跑 1–2B 模型或视觉任务。Axelera 转向生成式 AI 的 Europa，把 DRAM 带宽提高到约 6 倍（200 GB/s）。我们按 LPDDR5X 128-bit 规划，定位在 7B 级模型（§4.3）。
9. **派发开销是端侧 LLM 的隐形杀手。** AMD 在 XDNA2 上实测：每次派发 kernel 的开销可能比 kernel 本身还长，把每层 15 次派发合并成 3 次，decode 提速了 4 倍。我们的 Host-Device 接口必须支持一次提交覆盖整个 decode 步骤。
10. **驱动接口有现成的完整范例。**
    - Linux 内核里的 AMD amdxdna 驱动文档，完整描述了固件上下文、mailbox、PASID 隔离、分区和错误处理，可以直接作为 SPEC-330 / 400 的模板。
    - Hailo 数据手册里的 PCIe 端点特性清单，以及开源驱动里的 vDMA 描述符格式，可以直接作为 SPEC-310 / 320 的参考。
11. **端侧几家都在"同步写进 DMA 描述符"上殊途同归。** AMD 的 DMA 描述符里写明传输前后要获取和释放的锁；M100 的功能单元指令里写明要更新和等待的计数器。我们的 SPEC-250 / 320 应采用同样的做法。

---

## 1. 调度的静态程度：从完全静态到完全动态

```
完全静态 ◄──────────────────────────────────────────────────────────────► 完全动态
  Groq TSP            TPU v1               理想 M100              Tenstorrent         GPU
  编译器按周期排程      host 下发粗粒度        张量级指令              每个 Tile 跑多个      SIMT +
  无仲裁器、无 cache    CISC 指令            数据就绪即发射           RISC-V kernel，      cache
  启动时同步一次        4 级流水             计数器同步               硬件流控
```

**越靠左：**
- 硬件越简单，性能越可预测。
- 但对编译器要求越高，对不确定的延迟（DRAM、多任务并发）越不友好。

**越靠右：**
- 越灵活。
- 但每个 Tile 的控制开销（面积、功耗）越大，软件栈也越复杂。

M100 论文自称"编排式数据流（Orchestrated Dataflow）"，位置在中间：
- 编译器负责空间映射和粗粒度排序。
- 运行时按数据是否就绪来触发指令。
- 同一个功能单元内部按顺序执行，不同功能单元之间可以乱序。

---

## 2. 四家概览

| 维度 | Groq TSP（第一代） | Tenstorrent Wormhole / Blackhole | 理想 M100 NPU | Google TPU v1（及后续） |
|---|---|---|---|---|
| 定位 | 数据中心推理，多芯片扩展 | 数据中心训练与推理，以太网扩展 | 车载 SoC 内的 NPU，负责智驾和座舱 LLM | v1 是数据中心推理；v2 以后转向训练超算 |
| 工艺 / 面积 | 14nm，25×29 mm，900 MHz | WH 12nm；BH 6nm | TSMC N5A，399.8 mm²（整颗 SoC） | v1：28nm，700 MHz，典型功耗 40 W |
| 计算组织 | 功能切片：同类功能单元竖向堆成"slice"，数据横向流动 | 多种 Tile 在同一张 NoC 网格上：Tensix、DRAM、以太网、PCIe、ARC | 1 个中央控制块（CCB）+ 14 个 cluster，每个 cluster 4 个张量处理块（TPB） | 单个大核（v1）；v2 以后每芯片 1–2 个 TensorCore |
| 矩阵单元 | 4 个 320×320 MAC 阵列（int8；fp16 由两个平面配合完成） | Tensix 内的 Matrix Unit，以 32×32 tile 为单位运算 | 每个 TPB 一个 8×64 MAC 阵列，每个 MAC 每周期做 4 元素点积 | v1：256×256 脉动阵列；v2 以后：128×128 MXU |
| 向量单元 | 每 lane 16 个 ALU，全片 5120 个，可串接 | SFPU：32 宽 SIMD，32-bit lane | 可配置流水线（CVU），例如一次流过完成 softmax | v1 的激活单元；v2 以后为二维 SIMD 向量单元 |
| 标量 / 控制 | 144 个指令队列，编译器显式控制 | 每个 Tensix 5 个 RV32IM 小核；BH 另有 16 个 RV64 大核可跑 Linux | CCB：4 核 SiFive X280 + 自定义派发引擎；每个 cluster 1 个 X280 | host 下发 CISC 指令（v1）；v2 以后为 322-bit VLIW |
| 片上 SRAM | 220 MiB 全局共享，按 88 个 slice 分区 | WH：每 Tensix 1464 KiB；BH：每 Tensix 1536 KiB，全片约 241 MB | 每个 TPB 2 MB；CCB 共享 32 MB | v1：24 MiB 统一缓冲区 + 4 MiB 累加器 |
| 片外内存 | **无 DRAM**，模型全部放在 SRAM | WH：GDDR6，12 GiB，336 GB/s；BH：GDDR6，32 GB，512 GB/s | 8 路 LPDDR5X，64 GB，273 GB/s | v1：DDR3，8 GiB，34 GB/s；v2 以后为 HBM |
| 片上互联 | 没有路由器：数据每周期沿流寄存器移动一跳；纵向靠 SXM 换道 | 两张 NoC，方向相反，均为 2D torus；WH flit 为 256 bit；支持矩形广播和原子操作 | 2D Mesh（每对节点 256 GB/s）+ 数据环（DRB，确定性广播）+ 指令链总线（ICB） | 单核内部总线；芯片间用 ICI |
| 同步 | 只在程序开头做一次全片 barrier，之后完全靠编译器计算的时序 | Circular Buffer（带硬件流控的 SRAM 对象）+ 信号量 + NoC 原子操作 | 同步计数器：生产方加一，消费方等待计数达到期望值；另有 barrier | v1：指令里显式写同步 |
| 指令粒度 | 向量指令，并暴露每条指令的时序参数 | RISC-V kernel，加 Tensix 协处理器指令 | 张量级指令，长达数千 bit，执行时间达数万周期 | CISC 指令，平均每条 10–20 周期，一条可占用数千周期 |
| Host 接口 | PCIe Gen4 x16，带轻量 DMA | PCIe Tile：BAR + 可配置 TLB，映射到 NoC 坐标 | SoC 内部：2 个 AXI master（各 128 GB/s）+ AXI slave + 中断 | v1：PCIe Gen3 x16 协处理器 |
| 软件栈 | 编译器做全静态调度 | TT-Forge / TT-MLIR → TT-NN → TT-Metalium（C++ kernel）→ LLK | 时空调度器 + 图编译器 + 后端 C 扩展编译器 + 固件即时编译 | XLA |
| 数据类型 | int8、fp16 | 矩阵：块浮点（BFP2/4/8）、FP8、BF16、TF32、INT8；向量：FP32、INT16、INT32 | 论文给出 W4A16（decode）和 W8A8（prefill） | v1：int8；之后 bf16；Ironwood：FP8 |

---

## 3. 各家要点

### 3.1 Groq TSP

**核心思想：把传统 2D 多核网格"按功能切开"。**
- 传统多核是每个核里集齐各种功能单元。Groq 反过来，把同一种功能单元竖着堆成一个 slice，包括 MEM、VXM、MXM、SXM 和指令控制单元（ICU）。
- 指令从 ICU 向北流，数据在 slice 之间东西向流，指令和数据在流经的单元上相遇执行。
- 一行 slice 的横截面叫 superlane，宽 16 lane。20 个 superlane 拼成 320 元素的最大向量。

**确定性：**
- 硬件里没有仲裁器，也没有 cache。
- ISA 把每条指令的功能延迟（d_func）和操作数时序偏移（d_skew）暴露给编译器，编译器用 NOP 精确控制每条指令的发射时刻。
- 程序开始时用 Sync/Notify 做一次全片 barrier（35 周期），之后不再需要同步。

**存储：**
- 220 MiB SRAM 分成 88 个 slice，每个 slice 有 2 个 bank。
- bank 位直接暴露给编译器，由编译器手动安排访存并发。

**功能单元串接：**
- 例如 MEM → MXM → VXM，中间结果在流上直接传递，不写回 SRAM。
- 典型序列是 Read → Conv2D → Requantize → ReLU → Write。

**可靠性：**
- 生产方生成 ECC（每 128 bit 配 9 bit SECDED），消费方在使用前检查。
- 一套 ECC 同时覆盖 SRAM 和流数据通路，纠正过的错误记录到 CSR。

**多芯片（ISCA 2022）：**
- 软件调度的 Dragonfly 网络，最多 10,440 颗芯片。
- 链路上没有反压，由软件按时序安排每条链路。
- 硬件对齐计数器（HAC）加 DESKEW 指令，在异步时钟的芯片之间维持统一时间。

**对我们的价值：**
- 可借鉴：流式串接、bank 并发由编译器管理、生产方生成 ECC 并由消费方检查。
- 不可照搬：全静态调度要求所有数据都在延迟确定的 SRAM 里。我们的权重在 DRAM，DRAM 延迟不确定，这个前提不成立。

### 3.2 Tenstorrent（Grayskull → Wormhole → Blackhole）

**多种 Tile 在同一网格上。**
- Wormhole 有 80 个 Tensix、18 个 DRAM Tile、16 个以太网 Tile、1 个 PCIe Tile、1 个 ARC 管理 Tile，全部挂在两张 NoC 上。
- 有些坐标是"空 Tile"：只有路由器和 NIU。
- **这种"异构 Tile、同一张网格"的组织方式，可以直接作为我们 SPEC-100 的参考。**

**Tensix Tile 内部：**
- 5 个小 RISC-V 核（RV32IM）。典型分工是 2 个管 NoC 数据搬运，3 个驱动 Tensix 协处理器。
- Tensix 协处理器包括：2 个 Unpacker、矩阵单元（FPU）、向量单元（SFPU，32 宽 FP32 SIMD）、标量单元（ThCon）、4 个 Packer。
- L1 共 1464 KiB，分 16 个 bank，每个 bank 每周期可读或写 128 bit。
- L1 支持原子操作，包括条件原子（用于 FIFO 元数据），还支持近存累加。
- 窄写（小于 128 bit）是读-改-写操作，要占用 5 个周期。

**编程模型（TT-Metalium）：**
- 每个 Tensix 上跑 3 个 C++ kernel：1 个计算 kernel（自动拆成 3 个 RISC-V 线程）和 2 个数据搬运 kernel。
- kernel 之间用 Circular Buffer 同步，CB 是一个由硬件做流控的 SRAM 对象。

**NoC（来自 ISA 文档 `WormholeB0/NoC/`）：**
- flit 为 256 bit；一个包由 1 个头 flit 加最多 256 个数据 flit 组成，最大 8 KB。
- 每经过一个路由器 9 个周期，每个方向每周期传 1 个 flit。
- 虚通道号 4 bit：
  - 1 bit dateline，用于 torus 防死锁；
  - 2 bit 消息类：两类单播请求、一类广播请求、一类响应；
  - 1 bit buddy，可随拥塞情况变化。
- 写请求可以广播到一个矩形区域内的 Tensix。可以选择先做路径预留，避免广播死锁。
- 默认是弱序，可以通过选项加强。
- 常用事务由硬件保证不死锁；使用高级特性时，软件要承担一部分责任。

**PCIe Tile：**
- BAR0 512 MiB，BAR2 1 MiB，BAR4 32 MiB。
- 可配置的 TLB 决定 host 的一次读写落到哪个 NoC 坐标，也可以落到一个广播矩形。
- 有入站和出站两个方向的 iATU，PCIe 控制器内置 DMA 引擎。
- 主机侧请求超过 128 B 时，通常会被拆成多个 PCIe 事务。

**Blackhole 的变化：**
- 16 个 RV64 大核可以跑 Linux，定位从加速卡变成"独立 AI 计算机"。
- PCIe 升到 Gen5 x16。

**片上数据搬运带宽**（Blackhole HC36 讲稿）：
| 访问模式 | 带宽 |
|---|---|
| 本地 | 94 TB/s |
| 相邻 Tile | 47 TB/s |
| 行/列多播 | 24 TB/s |
| 3 跳 gather | 16 TB/s |
| 10 跳 gather | 5 TB/s |

说明数据局部性对性能影响极大。

**官方配套资源：**
- 官方模拟器兼 golden 参考模型 ttsim。
- tech_reports 里有 LLM、FlashAttention、DRAM 带宽饱和等专题报告。

**对我们的价值：**
- NoC、PCIe、L1 原子操作这几部分的文档细到寄存器级，**是写 SPEC-110 / 120 / 310 时最直接的参考**。
- 每个 Tile 5 个 RISC-V 核这一点要慎重：面积和软件复杂度都高。

### 3.3 理想马赫 M100 NPU

**组织：**
- 1 个中央控制块（CCB）加 14 个 TPB cluster，每个 cluster 4 个 TPB，共 56 个 TPB。
- 一个 cluster 内的 4 个 TPB 共享指令队列、ICB/DRB 节点和 1 个 RISC-V 向量 CPU（X280）。
- 共享这些资源，是为了把更多面积留给张量计算。

**中央控制块（CCB）：**
- 4 核 X280，每个核配一个自定义派发引擎，最多支持 4 个推理任务并发。
- 派发引擎把长达数千 bit 的 TPB 指令经指令链总线（ICB，菊花链，每周期 64 bit）下发。可以用目的掩码同时发给多个 TPB。
- 一条指令传输要几百个周期，执行却要数万个周期，所以派发不会成为瓶颈。
- 32 MB 共享 SRAM，分 4 个 bank，按 4 KB 交织。
- 2 个 DMA 负责 DDR 与 SRAM 之间的搬运，也能经数据环（DRB）把权重直接广播给 TPB。DRB 带宽 256 GB/s，和 DDR 读带宽相当。

**互联：**
- 2D Mesh：任意点对点通信，每对节点 256 GB/s，低拥塞时扩展性好。
- 数据环（DRB）：确定性的广播通路，聚合带宽 256 GB/s。
- 两者由软件按需选用。

**TPB 内部：**
- **高带宽共享存储（HBSM）**：2 MB，32 个 bank，每 bank 每周期 32 B，按 32 B 交织，8 个请求端口，延迟约 20 周期。
  - 它是所有功能单元之间的通信枢纽：生产方写入约定好的地址区间，再通过同步计数器通知消费方，不需要专用数据通路。
  - 一次访存赢得仲裁后即视为全局可见，同步动作就挂在这次访存上。
- **张量计算单元（TCU）**：
  - 8×64 个 MAC，每个 MAC 每周期做一次 4 元素点积，即每 TPB 每周期 2048 次乘加。
  - 激活值沿行复用，权重沿列复用。
  - 激活输入 32 B/周期，权重输入 64 B/周期。
  - 后面接非线性激活流水线。
- **可配置向量单元（CVU）**：由单功能的向量算子拼成可配置流水线，论文给出的例子是一次流过完成 softmax。
- **张量地址生成器（TWU）**：多层嵌套循环的地址发生器，每层配置初值、步长、终值，支持双缓冲。
- **DMA**：
  - DTDU 执行 TPB 指令，负责搬运、转置和填充。
  - GSDU 由 cluster CPU 控制，处理不规则的 gather/scatter。
- **同步单元（SU）**：管理硬件计数器。"更新"操作把计数器加一；"监视"操作等到计数器达到期望值才放行。

**指令发射：**
- 编译器输出的是"大致排好序"的指令流，执行由数据就绪和同步条件驱动。
- 同一个功能单元内部按序执行，跨功能单元可以乱序。

**软件：**
- 时空调度器：把大张量切成 mini-tensor，空间上分配到多个 TPB，时间上分阶段流水执行。
- 图编译器：做算子融合、布局变换和动态内存分配。
- 后端编译器：C 扩展编译器，生成调用张量计算、数据搬运、同步等硬件能力的内建指令。
- **NPU 固件在运行时即时编译 TPB 指令**，现场计算张量形状和地址。

**评测（12/14 cluster，与 Thor-U 同功耗对比）：**
| 负载 | M100 | Thor-U | 加速比 |
|---|---|---|---|
| UniAD 感知（8 cluster） | 30 FPS | 7.9 FPS | 3.8× |
| LLaMA2-7B decode（W4A16，1024 输入） | 21.34 ms | 20 ms | 0.94× |
| LLaMA2-7B prefill（W8A8，1024 token） | 79 ms | 154 ms | 1.95× |

decode 两者相当，论文解释为两边 DDR 带宽相同（都是 273 GB/s）。

**论文没有披露的内容：**
- 频率和峰值算力都没给。宣传口径是 1280 TOPS。
- 推算：按 TCU 规模，56 个 TPB 每周期约 11.5 万次乘加。按 INT8 稠密计算，要达到 1280 TOPS 需要约 5.6 GHz。所以 1280 TOPS 应该是 INT4、稀疏或其他口径，这一点未经证实。

**对我们的价值：** 这是**与我们场景最接近的公开设计**，同样是端侧、LPDDR5X、跑 LLM、2D Mesh、张量级指令、计数器同步。以下几项建议作为我们架构基线的主要参考：
- TPB 内部"共享 SRAM 作为通信枢纽"的组织方式；
- 张量地址生成器；
- 同步计数器的语义；
- 固件即时编译指令。

### 3.4 Google TPU

**TPU v1（ISCA 2017）：**
- 以 PCIe Gen3 x16 协处理器的形式接入服务器。论文自己的说法是"更像 FPU 协处理器，而不是 GPU"。
- host 把 CISC 指令写进指令缓冲区。主要指令有 Read_Host_Memory、Read_Weights、MatrixMultiply、Activate、Write_Host_Memory，平均每条 10–20 周期。
- 256×256 脉动阵列；24 MiB 统一缓冲区；4 MiB 累加器。
- 权重经 4 级深的 Weight FIFO 从 8 GiB DDR3（34 GB/s）读入。
- roofline 的拐点在每字节 1350 次运算，多数 MLP 和 LSTM 都落在带宽受限区。论文估计换成 GDDR5 能把性能提高到约 3 倍。
- **这是"小 batch 推理受带宽限制"的经典实证。**

**TPU v2/v3（CACM 2020、HC32）：**
- 改为 128×128 MXU、322-bit VLIW 核心顺序器、二维 SIMD 向量单元。
- 片外内存换成 HBM，带宽是 v1 的 20 倍。
- 引入 bf16 和芯片间互联 ICI。
- 设计取舍是"两个强核比很多弱核更容易让编译器生成高效代码"。

**TPU v4（ISCA 2023）：**
- 每颗芯片 2 个 TensorCore，共享 128 MiB Common Memory（CMEM）。
- 另有 SparseCore（加速 embedding）和光交换互联（OCS）。

**Efficiently Scaling Transformer Inference（MLSys 2023）：**
- 为 prefill 和 decode 建立延迟与成本的解析模型，用来选择多维切分策略。
- 结合多查询注意力（MQA）和 int8 权重，在 64 颗 TPU v4 上把 PaLM 540B 做到每 token 29 ms。
- **它的解析建模方法，可以直接用于我们的 DOC-002。**

**Ironwood（TPU v7，HC37）：**
- 首个双计算 die 的 TPU，FP8 算力 4614 TFLOPS。
- 8 堆 HBM3E，192 GiB，7.3 TB/s。
- 9216 颗芯片组成一个 pod。
- 这是数据中心产品，和端侧的直接关系不大，主要用来看行业趋势（FP8 普及、面向推理设计）。

**对我们的价值：**
- TPU v1 是 PCIe 推理加速器的鼻祖。它在 host 接口上的做法值得引以为戒：host 逐条下发指令，PCIe 延迟和 host 负载都会成为问题。
- Pope 等人的解析模型可以直接拿来用。

### 3.5 Hailo-10H（第二批）

**定位：和我们产品形态最接近的竞品。**
- M.2 Key M 模块（2242 / 2280），也有半高 PCIe 卡；PCIe Gen3 x4；模块上带 4 或 8 GB LPDDR4/4X。
- 40 TOPS（INT4）/ 20 TOPS（INT8），典型功耗 2.5 W。
- 上一代 Hailo-8 没有 DRAM 接口，模型必须全部放在片上；Hailo-10H 加了 DRAM 接口，才能跑 LLM。

**架构（官方只有定性描述）：**
- 官方称为"结构驱动的数据流架构"：计算、存储、控制资源分布在芯片各处，编译时按网络的层来分配，层与层之间形成流水。
- Hailo-8 数据手册补充的细节：
  - 8 个 cluster，可以按 cluster 关断电源；
  - 权重和激活值按层可配为 8 或 16 bit，权重还可以配为 4 bit；
  - 有专门的预处理和后处理硬件引擎，包括 NMS、双线性插值、reshape、softmax；
  - 两个 Cortex-M4 MCU（200 MHz，640 KB 带 ECC 的 SRAM），以及安全启动、加密引擎、防火墙。

**PCIe 端点特性**（Hailo-8 数据手册 §5.4，可以直接当作我们 SPEC-310 / 320 的需求清单）：
- Gen3 x4，可降为 x1 或 x2；单物理功能、单虚通道；最大载荷 256 B；最多 32 个未完成的读请求。
- 中断：MSI 最多 32 个，也支持传统中断；支持 AER 和 ECRC。
- DMA：内部 32 个通道，scatter-gather 可动态配置，每一页都可以单独配置中断和起止条件；另有 16 对低延迟的流式收发通道。
- 可以通过 PCIe 切换核心上下文（包括配置和数据），也可以从 PCIe 加载固件。
- 电源管理：ASPM L0s、L1，L1.1 和 L1.2 子状态，LTR，D0/D3。
- 启动方式由引脚选择：PCIe 启动（片内 ROM 配合 host 驱动经 PCIe 拉取固件），或 SPI Flash 启动。

**驱动**（`hailo/hailort-drivers/` 源码）：
- BAR 划分：BAR0 是配置空间，BAR2 是 vDMA 寄存器，BAR4 是固件访问窗口。
- vDMA 描述符 16 B，四个字段依次是：页大小和控制位、地址低位和数据 ID、地址高位、剩余长度和状态（由设备回写）。每个 DMA 引擎 32 个通道。
- 固件控制命令通过 BAR 读写；中断源从 BAR 读出；启动阶段最多用 16 个 vDMA 通道下载固件。

**LLM 实测：**
- 第三方（arXiv 2603.23640）：树莓派 5 加 Hailo-10H 跑 Qwen2.5-1.5B（4-bit），持续 6.9 tok/s，系统功耗不到 2 W。论文判断瓶颈在模块上的内存带宽。
- 厂商口径：2B 级模型首 token 延迟不到 1 s，decode 超过 10 tok/s。

**对我们的价值：**
- 它代表了"M.2 端侧 LLM 卡"的下限：功耗只有 2.5 W，但 LPDDR4 的带宽只够跑 1–2B 模型。我们按 LPDDR5X 128-bit、15–25 W 规划，瞄准 7B 级模型，定位高一档。
- PCIe 端点特性清单、vDMA 描述符格式、BAR 划分、两种启动方式，可以直接借鉴到 SPEC-310 / 320 / 330。

### 3.6 Axelera Metis / Europa（第二批）

**Metis（2024，TSMC 12nm）：**
- 4 个 AI Core，每个 AI Core 包括：
  - 矩阵向量乘单元，基于 SRAM 的数字存内计算（D-IMC）：权重存储单元和计算单元密集交织；INT8 权重和激活，INT32 累加。
  - 数据处理单元（DPU）：逐元素向量运算、激活函数。
  - 深度卷积单元（DWPU）：深度卷积、池化、上采样。
  - 权重解压单元。
  - 4 MiB L1 SRAM，以及一个驱动数据流引擎的 RISC-V 控制核。
- 两条可以并行工作的流式数据通路，矩阵向量乘一条，深度卷积一条。
- **后台加载权重**：存内计算阵列可以存 4 组权重，一组在算的同时，另一组可以加载。
- 32 MiB 共享 L2 SRAM，全片片上存储共 52 MiB；NoC 到共享存储的带宽为 1 Tbit/s。
- 一个 RISC-V 系统控制器跑实时操作系统，负责启动芯片、管理外设和 AI Core。安全模块负责安全启动以及权重和数据的加密。
- 210–214 TOPS（INT8，0.8 GHz），15 TOPS/W，平均功耗 5–9 W；PCIe Gen3 x4；LPDDR4x，34 GB/s。

**Europa（2025-10 发布，计划 2026 年出货）：**
- 8 个第二代 AI Core（存内计算加大型向量引擎），629 TOPS（INT8），45 W。
- 两组各 8 个 RISC-V 向量核，负责预处理和后处理。
- 128 MB L2 SRAM；256-bit LPDDR5，200 GB/s；集成视频解码。
- 定位"边缘与服务器级的生成式 AI"。

**对我们的价值：**
- **从 Metis 到 Europa 的变化，又一次证明 LLM 需要 DRAM 带宽。** 做视觉的 Metis 只有 34 GB/s；转向生成式 AI 的 Europa 把 DRAM 带宽提高到约 6 倍，L2 提高到 4 倍。
- 存内计算的优势在于权重常驻片上、被反复使用的场景（如 CNN）。LLM decode 时每个权重只用一次，存内计算省下的那部分读权重能耗基本不存在。**不建议我们走存内计算路线。**
- "后台加载下一组权重"和"权重在线解压"这两个机制，可以借鉴到我们矩阵引擎的权重输入通路上。
- 每个计算核配一个 RISC-V 控制核、系统级再配一个跑 RTOS 的 RISC-V 控制器，这种分工和我们"管理 Tile 加计算 Tile"的结构一致。

### 3.7 AMD XDNA / XDNA2（第二批）

**阵列结构：**
- 二维 Tile 阵列。每一列从上到下依次是：4 个计算 Tile、1 个存储 Tile（L2）、1 个 Shim Tile（连接 SoC 总线和 DRAM）。
- XDNA（Phoenix / Hawk Point）是 4×5，共 20 个计算 Tile，10–16 TOPS；XDNA2（Strix Point）是 4×8，共 32 个计算 Tile，50 TOPS。
- 计算 Tile：VLIW 加 SIMD 的处理核（AIE-ML），有独立的程序存储，数据存储（L1）64 KB。
- 存储 Tile：每个 512 KB，一整行组成软件管理的 L2。Phoenix 共 2560 KB，Strix 共 4096 KB。
- 数据类型：int8、int16、bf16。XDNA2 增加了块浮点 bfp16，8 个数共享一个指数。

**数据搬运与同步：**
- 每个 Tile 都有 DMA。计算 Tile 和 Shim Tile 各有 2 个读出通道（MM2S）和 2 个写入通道（S2MM），存储 Tile 各有 6 个。
- DMA 由一串缓冲区描述符（BD）编程。每个 BD 写明搬运长度、地址、多维寻址参数（计算 Tile 和 Shim Tile 支持 3 维，存储 Tile 支持 4 维），以及**传输前要获取、传输后要释放的硬件锁**。也就是说，同步直接写在 DMA 描述符里。
- Tile 之间用可配置的流交换网络连接，可以走电路交换，也可以走包交换。相邻计算 Tile 之间有 cascade 接口，用来传递部分累加和。

**固件与 host 接口**（`amd-xdna/linux-kernel-amdxdna-doc/`）：
- NPU 内有一个微控制器跑固件，负责命令处理、阵列分区和配置、上下文管理、执行编排。
- 固件里有两类相互隔离的执行环境：
  - **MERT**：全局唯一的特权环境，处理驱动发来的管理命令，包括建立上下文、telemetry、查询和错误处理。
  - **ERT**：每个工作负载上下文一个，非特权，执行该上下文的 ctrlcode。
- 每个上下文有自己的 mailbox 和 MSI-X 中断向量。mailbox 的环形缓冲区在 SRAM BAR 里，头尾指针等控制寄存器在 Mailbox BAR 里。
- 阵列可以按列切成互相隔离的空间分区。每个分区绑定一个 PASID，DMA 访问 host 内存时由 IOMMU 按 PASID 做隔离。分区可以由一个上下文独占，也可以由多个上下文分时共享。
- 一个程序由两部分组成：
  - overlay：配置流交换网络，并加载各计算 Tile 的 ELF；
  - ctrlcode：由 ERT 执行的编排操作码序列，其中包括驱动 host 与 L2 之间的 DMA。
- 每个上下文在 host 内存里有一块 64 MB 的指令缓冲区，用来存放 ctrlcode。
- 启动：驱动经 PSP 加载签过名的固件，然后在 BAR0 的指定位置等待"存活"信号。
- 出错处理：MERT 暂停出错的上下文并通知驱动；驱动提供一块缓冲区，MERT 把出错分区的寄存器状态转储进去，由驱动解析。

**LLM 实测**（开源工具链，arXiv 2606.07586）：
- XDNA2 跑 Llama-3.2-1B（BF16）：2048 token 的 prefill 首 token 延迟 1.3 s，decode 10.8 tok/s。
- **最关键的教训是派发开销。** 每次派发 kernel 都要经过应用、runtime、驱动、固件、硬件好几层，开销可能比 kernel 本身还长。作者把每层的派发次数从 15 次合并到 3 次，再加上复用缓冲区、对齐数据布局以避免 host 端转置，decode 才提速了 4 倍。
- GEMM 实测（arXiv 2512.13282）：XDNA2 上 int8 达到 38 TOPS，约为峰值 50 TOPS 的 76%；bf16 达到 14.7 TOPS。

**对我们的价值：**
- **amdxdna 文档可以直接作为我们 SPEC-330 / 400 的模板**：特权与非特权的固件上下文、每个上下文独立的 mailbox 和中断、PASID 隔离、分区与上下文的绑定、出错时的状态转储流程。
- 描述符里带锁，和 M100 的同步计数器是同一个思路，进一步支持我们在 DMA 描述符里直接写"等待/通知"字段。
- 派发开销必须在架构阶段就解决：一次提交要能覆盖整个 decode 步骤，不能每个 kernel 都往返 host。

---

## 4. 用调研数据校准我们的 KPI（推算）

前提：LLaMA2-7B 约 67.4 亿参数；32 层，hidden 维度 4096，标准多头注意力。

### 4.1 decode（参照 M100 的实测）

| 项目 | 数值 | 说明 |
|---|---|---|
| W4 权重读取量 | 约 3.4 GB | 67.4 亿 × 0.5 B，未计分组 scale |
| KV cache 读取量（1024 上下文，FP16） | 约 0.54 GB | 2 × 32 层 × 4096 × 1024 × 2 B |
| 每 token 总读取量 | 约 3.9 GB | |
| M100 带宽下限（273 GB/s） | 约 14.3 ms | 3.9 GB ÷ 273 GB/s |
| M100 实测 | 21.34 ms | |
| **有效带宽利用率** | **约 67%** | 14.3 ÷ 21.34 |
| 我们的候选：128-bit LPDDR5X-8533 | 136.5 GB/s | |
| 我们的带宽下限 | 约 28.6 ms/token | |
| 按 67% 效率推算 | 约 43 ms/token，**约 23 tok/s** | |

**结论：**
- 我们之前定的"7B 模型 decode ≥ 20 tok/s"可以达到。
- 前提是 DRAM 有效带宽利用率 ≥ 57%，也就是每 token 3.9 GB、50 ms 内读完需要 78 GB/s。
- 如果 KPI 要提到 30 tok/s 以上，就得考虑 256-bit 位宽或 KV cache 量化。
- 67% 可能偏保守。Tenstorrent 的技术报告显示，专门优化后 DRAM 带宽利用率可以超过 92%：每个 bank 配一个读取者，并放在离 bank 一跳的位置（见 [tenstorrent/NOTES.md](tenstorrent/NOTES.md)）。按 80% 估算，我们约为 28 tok/s。**Memory Tile 在 Mesh 上的摆放，以及权重怎样按 bank 分片，直接决定这个效率能做到多少，应列入 gem5 设计空间探索。**
- Tenstorrent 的矩阵引擎在输入只有 1 行时，有效吞吐只剩 1/8，印证了"大阵列做 batch=1 的 GEMV 效率低"。我们的矩阵引擎形状要针对 decode 专门设计（SPEC-210）。

### 4.2 prefill（参照 M100 的实测）
- 1024 token 的运算量约 2 × 67.4 亿 × 1024 ≈ 13.8 TOPs，算上注意力约 14.4 TOPs。
- M100 用时 79 ms，折合有效算力约 180 TOPS（INT8）。
- 如果我们的目标是 1k prompt 的首 token 延迟（TTFT）小于 1 s，有效算力只需约 15 TOPS。prefill 不会成为端侧的瓶颈，**算力规格主要应该由面积和功耗预算决定，而不是由 prefill 决定。**

### 4.3 端侧竞品定位

| | Hailo-10H | Axelera Metis | Axelera Europa | AMD XDNA2 | 理想 M100 | 我们（当前默认） |
|---|---|---|---|---|---|---|
| 形态 | M.2 模块 / 半高卡 | M.2 模块 / PCIe 卡 | PCIe 卡 | 集成在 PC 处理器内 | 集成在车载 SoC 内 | PCIe 卡（具体形态由 DOC-001 定） |
| Host 接口 | PCIe Gen3 x4 | PCIe Gen3 x4 | PCIe（代数未公布） | SoC 内部总线 | SoC 内部总线 | PCIe Gen4 x4 |
| 峰值算力 | 40 TOPS INT4 / 20 TOPS INT8 | 214 TOPS INT8 | 629 TOPS INT8 | 50 TOPS INT8 | 1280 TOPS（宣传口径） | 由 DOC-002 推导 |
| 功耗 | 典型 2.5 W | 平均 5–9 W | 45 W | — | — | 15–25 W |
| 片外内存 | LPDDR4/4X，4–8 GB | LPDDR4x，34 GB/s | 256-bit LPDDR5，200 GB/s | LPDDR5（与 CPU 共享），约 120 GB/s | LPDDR5X，273 GB/s，64 GB | 128-bit LPDDR5X，约 136 GB/s，8–16 GB |
| 片上 SRAM | 未公布 | 16 MiB L1 + 32 MiB L2 | 128 MB L2（另有 L1） | 2 MB L1 + 4 MB L2 | 112 MB（TPB）+ 32 MB（CCB） | 待定 |
| LLM 实测 | Qwen2.5-1.5B（4-bit）6.9 tok/s（第三方） | 未公布 | 宣称可跑 80B 以内的 LLM | Llama-3.2-1B（BF16）10.8 tok/s（开源工具链） | LLaMA2-7B（W4A16）约 47 tok/s | 7B（W4）约 23 tok/s（§4.1 推算） |

**解读：**
- 按功耗看，我们落在 Metis 和 Europa 之间；按带宽看，我们是 Metis 的 4 倍，约为 Europa 的 2/3（Hailo-10H 没有公布带宽）。
- 端侧 LLM 的公开实测目前只到 1–2B 模型（Hailo-10H、XDNA2）。7B 级跑到 20 tok/s 以上的公开数据只有 M100，而 M100 是车载 SoC，带宽是我们的 2 倍。
- 也就是说，在这次调研到的公开资料里，还没有"PCIe 卡形态、跑 7B 级模型、20 tok/s 以上"的产品。这可以作为 DOC-001 产品定位的依据。
- 注意：表中数据口径不一（有厂商宣称、有第三方实测、精度也不同），只能用于定位，不能直接横向比较。

---

## 5. 对我们设计的启示（按 SPEC 编号对应）

| 我们的设计问题 | 参考做法 | 建议 | 影响文档 |
|---|---|---|---|
| Tile 种类与布局 | Tenstorrent：异构 Tile 同在一张网格；M100：4 个 TPB 组成 cluster，共享资源 | Compute、Memory、PCIe、管理四种 Tile 放在同一张 Mesh 上；另外用 gem5 评估"2×2 Compute Tile 组成 cluster、共享 CPU 和指令缓冲"的方案 | SPEC-100 |
| 良率冗余 | M100 实测只用 12/14 个 cluster；Wormhole 按产品屏蔽部分 Tensix | 定义 Tile 屏蔽机制、逻辑坐标到物理坐标的重映射，以及 NoC 路由绕开被屏蔽 Tile 的规则 | SPEC-100 / 120 |
| **Tile 控制方式**（待决事项） | A：Tenstorrent，每 Tile 多个 RISC-V 跑 kernel；B：M100，集中派发张量级指令，每 Tile 有指令队列、功能单元内按序、跨单元靠计数器同步，cluster 共享 CPU 处理不规则算子 | **建议采用 B 为主。** 理由：控制面积小；编译器只需输出张量级指令；和 gem5 按张量指令建模的粒度一致；有端侧 LLM 的实证。风险：性能依赖编译器时空调度的质量，新算子要靠 cluster CPU 兜底 | SPEC-200 / 230，MAN-020 |
| 矩阵引擎 | M100 TCU：8×64 MAC × 4 元素点积，流式输入权重；TPU v1 和 Groq：超大脉动阵列 | 每 Tile 配中等规模的点积阵列，激活沿行复用、权重沿列复用。权重以流的形式输入，并在线做 INT4 反量化。超大阵列对 batch=1 的 decode 不友好，TPU v1 自己就受带宽限制 | SPEC-210 |
| 向量引擎 | M100 CVU 可配置流水线；Groq VXM 的 ALU 串接；Tenstorrent SFPU 加查表 | 采用可串接或可配置的流水线，让 softmax、RMSNorm、RoPE 一次流过完成；内部用 FP32；超越函数用分段多项式或查表实现 | SPEC-220 |
| Tile 内存储 | M100 HBSM：2 MB，32 bank × 32 B；Wormhole L1：1464 KiB，16 bank × 16 B | 每 Tile 1.5–2 MB 分 bank 的 SRAM，作为各功能单元之间的通信枢纽；明确"访存赢得仲裁即全局可见"这类顺序语义 | SPEC-200 / 110 |
| 地址生成 | M100 TWU：多层循环加双缓冲 | 片上 DMA 和各功能单元统一使用 N 层循环的地址发生器，描述符格式全片一致 | SPEC-200 / 250 / 320 |
| 片上共享 SRAM | M100 CCB：32 MB，按 4 KB 交织 | 评估是否设一块共享 SRAM，用于权重中转或 KV cache 热区（交给解析模型和 gem5 去定） | SPEC-100 / 300，DOC-002 |
| NoC | Tenstorrent：256-bit flit，消息类与虚通道绑定，矩形广播，原子操作，顺序可选；M100：Mesh 加确定性广播环 | 定义单播请求、多播请求、响应三类消息，各自走独立虚通道。用 Mesh（不用 torus），XY 路由天然无死锁，不需要 dateline。**权重广播是走 Mesh 多播还是专用广播通路，作为 gem5 设计空间探索的第一优先级** | SPEC-120，DOC-005 |
| Tile 间同步 | M100 同步计数器；Tenstorrent CB 加信号量；M100、Groq 的 barrier | 统一采用计数器语义：更新为加一，等待为计数达到 N；远程 Tile 的信号量通过 NoC 原子操作实现；另外提供全局 barrier | SPEC-250 |
| Host 接口与地址空间 | Tenstorrent：BAR0 用 TLB 窗口映射到 NoC 坐标，BAR2 放控制寄存器，BAR4 做别名，支持广播窗口；TPU v1：host 逐条下发指令 | 采用 TLB 窗口方案。host 只提交命令缓冲区，由管理 Tile 上的固件派发，**不要让 host 逐条下发指令** | SPEC-110 / 310 / 400 |
| 管理 Tile 与固件 | M100 CCB 固件即时生成指令、现场计算地址；Tenstorrent 的 ARC Tile 只管芯片管理 | 管理 Tile 固件负责派发张量指令，并在运行时修补形状和地址。LLM decode 每一步 KV 长度都会变，这个能力可以避免每生成一个 token 就重新编译 | SPEC-330 / 400 |
| 数值与数据类型 | M100：W4A16（decode）、W8A8（prefill）；Tenstorrent：块浮点、FP8、TF32；Groq：320 元素求和只在最后舍入一次 | 支持 INT4（带分组 scale）权重、INT8、FP16/BF16、FP8 E4M3，累加用 FP32/INT32；块浮点作为可选的权重压缩格式 | SPEC-240，DOC-001 |
| 可靠性（RAS） | Groq：生产方生成 ECC，消费方检查，纠错记入 CSR | SRAM 和片上数据通路共用 ECC，纠错次数计入性能计数器 | SPEC-340 / 350 |
| 多任务并发 | M100：4 个任务并发；cluster 划分为 8 个给智驾、6 个给座舱，互相隔离 | Host-Device 接口支持把 Tile 划分成多个上下文，例如语音识别、LLM、语音合成同时运行 | SPEC-400 |
| 编译器分工 | M100 时空调度器配合 mini-tensor；TT-Metalium 的 kernel 模型；Pope 的解析模型 | 编译器团队的映射策略以"时空调度"为基础；我们的解析模型沿用 Pope 的方法 | MAN-020，DOC-002 |
| 建模方法 | Tenstorrent 有官方模拟器兼 golden 模型 ttsim；M100 用 HAPS-100 做 FPGA 原型 | 印证了"功能核心库同时用于 gem5 和 golden 模型"的思路；gem5 按张量指令粒度建模 | DOC-004，MAN-060 |
| **以下为第二批新增** | | | |
| 固件上下文与隔离 | AMD：一个特权的 MERT 加每上下文一个 ERT；mailbox 和 MSI-X 按上下文独立；分区绑定 PASID | 管理 Tile 固件分为特权的管理上下文和非特权的用户上下文；每个用户上下文有独立的命令队列、doorbell 和 MSI-X 向量；空间分区绑定地址空间 ID，DMA 访问 host 内存时由 IOMMU 隔离 | SPEC-330 / 400 / 310 |
| PCIe BAR 划分 | Tenstorrent：BAR0 为 TLB 窗口，BAR2 为寄存器，BAR4 为别名；Hailo：BAR0 配置，BAR2 vDMA 寄存器，BAR4 固件访问；AMD：按 PSP、SMU、mailbox 环、mailbox 寄存器、公共寄存器分类 | 初步方案：BAR0 放控制寄存器、doorbell 和 mailbox；BAR2 为 64-bit 可预取的 TLB 窗口，映射到 NoC 坐标和 DRAM。MSI-X 表的位置由 SPEC-310 定稿 | SPEC-110 / 310 |
| Host DMA 特性 | Hailo：32 个通道，16 B 描述符（含状态回写），scatter-gather 动态可配，每页可配中断 | 以 Hailo 的特性清单为基线：多通道、描述符环放在 host 内存、状态回写、中断合并。描述符附带"等待/通知"计数器字段，和片上 DMA 的格式统一 | SPEC-320 |
| 启动方式 | Hailo：引脚选择 PCIe 启动或 SPI Flash 启动；AMD：PSP 验签后启动，BAR0 握手；Axelera：RISC-V 系统控制器跑 RTOS | 同时支持 PCIe 启动（由驱动下发固件）和 SPI Flash 启动；固件必须验签；在 BAR 上定义固件就绪的握手寄存器 | SPEC-330 / 130 |
| 派发开销 | AMD XDNA2：每层派发从 15 次合并到 3 次，decode 提速 4 倍；M100：固件在运行时即时生成指令 | Host-Device 接口支持一次提交覆盖整个 decode 步骤（全部层），由设备端固件循环执行。把"单次提交开销"写进 DOC-001 作为 KPI | SPEC-400，DOC-001 |
| 权重输入通路 | Axelera：后台加载下一组权重、权重在线解压；XDNA2、Tenstorrent：块浮点 | 矩阵引擎的权重输入通路内置反量化和解压，并支持双缓冲：算当前这组权重时预取下一组 | SPEC-210 / 240 |
| 预处理与后处理 | Hailo：NMS、softmax、缩放等专用硬件引擎；Europa：RISC-V 向量核集群 | LLM 场景下，采样（top-k / top-p）和 embedding 查表在设备端完成，避免每生成一个 token 都回 host。管理 Tile 或 cluster CPU 是否需要向量扩展，交给 DOC-002 评估 | SPEC-100 / 330 |

---

## 6. 本轮调研的局限与下一步

**局限：**
- ~~TPUv4i 的"Ten Lessons"论文没有拿到~~：后来在合著者的个人主页找到了正式版，已收录，十条经验整理在 [LESSONS.md](LESSONS.md)。
- M100 只有一篇论文，频率、峰值算力、NoC 细节都没有披露。
- 以上 KPI 推算只用了一个公开数据点。

**第二批的局限：**
- Hailo 没有公开任何架构细节，只有定性描述；Hailo-10H 的 DRAM 位宽和带宽也没有公布。
- Axelera Europa 只有新闻稿。
- AMD 计算阵列的官方架构手册 AM020 / AM027 没拿到，原因是网络访问不了（见 README）。目前 XDNA 的架构细节来自论文和驱动文档。

**建议后续补充：**
- 补齐缺失的关键资料：AM020 / AM027、AMD 的 IEEE Micro 2024 论文。
- **SambaNova SN40L**：数据流架构加多级存储，专门应对 LLM 的"内存墙"。
- M100 引用的其他数据流架构：Cerebras、Tesla Dojo/FSD、Meta MTIA。
