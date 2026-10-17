# 阅读笔记：Google TPU

| 项目 | 内容 |
|---|---|
| 相关性 | ★★★（TPU v1 是 PCIe 推理加速器的原型；Pope 的解析模型可直接用于 DOC-002）／★（v4 和 Ironwood 是数据中心训练和超大规模系统，只看趋势） |
| 对应文档 | DOC-002，SPEC-100 / 200 / 210 / 220 / 240 / 330 / 350 / 400 |

## 资料与引用代号

表中"出处"一列写作"代号 p.页码"，页码对应 PDF 页，也对应同名 `.md` 里的 `<!-- p.N -->` 标记。

| 代号 | 资料 |
|---|---|
| V1 | [2017-isca-tpuv1/](2017-isca-tpuv1/)：In-Datacenter Performance Analysis of a TPU（ISCA 2017） |
| CACM | [2020-cacm-tpuv2v3-supercomputer/](2020-cacm-tpuv2v3-supercomputer/)：A Domain-Specific Supercomputer for Training DNNs（TPU v2/v3） |
| HC32 | [2020-hotchips32-tpuv2v3/](2020-hotchips32-tpuv2v3/)：TPUv2 与 TPUv3 讲稿，70 页，信息多在图里 |
| V4i | [2021-isca-tpuv4i-ten-lessons/](2021-isca-tpuv4i-ten-lessons/)：TPUv4i（推理专用）与十条经验（ISCA 2021）——**与我们最相关的一篇 TPU 论文** |
| TALK | [2022-patterson-tpu-lessons-talk/](2022-patterson-tpu-lessons-talk/)：Patterson 讲四代 TPU 的十条经验（另含碳排放部分） |
| V4 | [2023-isca-tpuv4/](2023-isca-tpuv4/)：TPU v4（ISCA 2023） |
| POPE | [2023-mlsys-scaling-transformer-inference/](2023-mlsys-scaling-transformer-inference/)：Efficiently Scaling Transformer Inference（MLSys 2023） |
| HC37 | [2025-hotchips37-ironwood/](2025-hotchips37-ironwood/)：Ironwood（TPU v7）讲稿 |

## TPU v1：PCIe 推理加速器的原型（V1）

| 参数 | 数值 | 出处 |
|---|---|---|
| 形态 | PCIe Gen3 x16 协处理器，可以像 GPU 一样插进现有服务器；"更像 FPU 协处理器，而不是 GPU" | p.2–3 |
| 工艺 / 频率 / 功耗 | 28 nm，700 MHz，TDP 75 W，典型 40 W | p.5 |
| 矩阵单元 | 256×256 个 8-bit MAC（共 65,536 个），峰值 92 TOPS；脉动阵列，目的是减少对统一缓冲区的读写、节省能耗 | p.1、p.3–4 |
| 片上存储 | 24 MiB 统一缓冲区（存中间结果）+ 4 MiB 累加器，共 28 MiB，软件管理 | p.1、p.3 |
| 片外存储 | 8 GiB DDR3（权重存储），34 GB/s；经 4 级深的 Weight FIFO 送入矩阵单元 | p.3 |
| 指令 | host 经 PCIe 把指令写入指令缓冲区；CISC 风格，带重复字段，平均每条 10–20 周期；MatrixMultiply 指令 12 B | p.3–4 |
| 主要指令 | Read_Host_Memory、Read_Weights、MatrixMultiply/Convolve、Activate、Write_Host_Memory；另有同步、中断 host、调试标记等 | p.3–4 |
| 流水线 | 4 级流水，每条指令在一个独立的执行单元里执行，一条指令可占用单元数千个周期 | p.4 |
| roofline 拐点 | 每取 1 字节权重要做 1350 次运算才能达到峰值；多数应用落在带宽受限区 | p.6 |

**教训**
- **带宽是瓶颈**：改用 GDDR5 能让权重带宽提高 5 倍以上，性能约提到 3 倍；统一缓冲区也可以从 24 MiB 缩到 14 MiB（p.12）。
- **推理看重的是尾延迟，而不是吞吐**：开发者对响应时间有严格要求，99 分位延迟的约束会把 batch 压得很小（p.8、p.15）。
- **性能计数器不能事后再补**：这是论文列出的陷阱之一（p.13）。**对我们的 SPEC-350 和 gem5 校准很重要。**
- **成功的原因**（p.15）：矩阵单元"大，但不过大"；大容量软件管理的片上存储；能跑完整个推理模型、减少对 host CPU 的依赖；单线程、确定性的执行模型，正好匹配 99 分位延迟的要求。

## TPU v2 / v3：从推理芯片走向训练（CACM、HC32）

**相对 v1 的改动**（HC32 p.13–24）
- 用一块统一的向量存储，取代各固定功能单元之间的缓冲区（p.16）。
- 用通用向量单元，取代固定功能的激活流水线（p.18）。
- DRAM 接入存储系统，不再直接接到矩阵单元（p.22）。
- 增加芯片间互联（p.24）。

| 参数 | 数值 | 出处 |
|---|---|---|
| 核心 | 每芯片 2 个 TensorCore；理由是"两个强核比很多弱核更容易让编译器生成高效代码" | CACM p.4 |
| 指令 | 322-bit VLIW：2 个标量槽、4 个向量槽（其中 2 个做 load/store）、2 个矩阵槽（push/pop）、1 个杂项槽、6 个立即数；没有指令 cache，由 XLA 以 overlay 方式加载指令存储 | HC32 p.29；CACM p.4–5 |
| 矩阵单元 | 128×128 脉动阵列（v1 是 256×256），左操作数和结果流式进出，右操作数驻留；v3 每核 2 个 | HC32 p.32；CACM p.5–6 |
| 数值 | bf16 乘法（1 位符号、8 位指数、7 位尾数），fp32 累加；bf16 乘法器的面积和能耗约为 fp16 的一半 | HC32 p.32；CACM p.7 |
| 向量单元 | 向量存储 16 MiB（32K 个 128×32-bit 元素） | CACM p.5 |
| 转置单元 | 做 128×128 转置、归约、置换，在向量 lane 之间重排数据 | CACM p.6 |
| 存储系统 | 对 SRAM 暂存器做 load/store，调度可预测；可以等待同步标志；由异步 DMA 访问，完成时置同步标志 | HC32 p.35 |
| 片外存储 | HBM，带宽是 v1 的 20 倍 | CACM p.4 |
| 互联 | 片上路由器，4 条链路，每条约 500 Gbps，组成 2D torus；软件视角下和 HBM 一样用 DMA 访问，只能 push | HC32 p.40；CACM p.3 |

## TPUv4i：推理专用 TPU（V4i）

十条经验及其对我们的意义，整理在 [../LESSONS.md](../LESSONS.md) 第一部分第 4 节，这里只记设计参数和取舍。

| 设计点 | 内容 | 出处 |
|---|---|---|
| 定位 | 单核推理芯片，与双核训练芯片 TPU v4 共用同一个核和同一套 uncore 代码，由同一个团队开发 | p.6 |
| 兼容策略 | 只保证编译器兼容，不保证二进制兼容；VLIW 指令比 v3 宽 25% | p.6、p.8 |
| 片上存储 | 新增 128 MB 共享存储（CMEM），占芯片面积 28%；128 MB 是性能与面积折中的拐点；芯片面积小于 400 mm² | p.6–7 |
| DMA | 4 维张量 DMA（三重步长），每一维的步数和正负步长都可配置，源端和目的端独立配置；带宽与步长参数无关；本地、芯片间、host 三种 DMA 统一；读写完全无序，靠显式同步，支持按部分完成进度同步 | p.7 |
| 片上互联 | 用共享的片上互联取代点对点连线；原生访问宽度 512 B；HBM、CMEM、VMEM 各分成 4 组 128 B，形成 4 张互不重叠的子网络 | p.8 |
| 矩阵单元 | 每核 4 个 MXU；4 输入浮点加法器，脉动阵列关键路径降到 1/4，面积省 40%，功耗省 25%，MXU 峰值功耗降 12% | p.8 |
| 数值 | 保留 bf16 和 fp32 以保证与训练一致，另加 int8 以兼容为 TPU v1 量化过的应用 | p.8 |
| 功耗 | 1.05 GHz，TDP 175 W，可以风冷 | p.8 |
| 可观测性 | 大量 trace 和性能计数器，尤其在 uncore 部分 | p.8 |

## TPU v4（V4）

- 每芯片 2 个 TensorCore，每个 TensorCore 有 4 个 128×128 MXU；两个 TensorCore 共享 128 MiB 的 Common Memory（CMEM）（p.2）。
- SparseCore 加速 embedding；光交换机（OCS）可以重构 4096 芯片的拓扑（p.1–2）。
- 与端侧关系不大。值得注意的是，片上共享一块大 SRAM（CMEM）的做法，和 M100 的 CCB 32 MB SRAM 是同一思路。

## Pope 等：Transformer 推理的解析模型（POPE）

- **成本模型**（p.3）：
  - 存储时间：每生成一个 token，要把权重和 KV cache 从 HBM 读一遍。
  - 计算时间：N 参数的 decoder 模型，每个 token 约 2N 次 FLOP。
  - prefill 可以在输入长度上并行，decode 只能逐个 token 顺序执行，所以两者要分开分析。
- **KV cache 可能比权重还大**：batch 512、上下文 2048 时，KV cache 总量 3 TB，是参数量的 3 倍（p.3）。多查询注意力（MQA）能大幅缩小 KV cache。
- **切分策略**（p.4–5）：1D 权重驻留、2D 权重驻留、权重收集（weight-gathered）。三者在通信量和 batch 大小之间取舍不同。
- **结果**：64 颗 TPU v4 跑 PaLM 540B（int8 权重），每 token 29 ms；大 batch prefill 的 MFU 达到 76%（p.2）。
- **对我们的用法**：把"多芯片"换成"多 Tile"，这套解析框架可以直接用于 DOC-002，评估 LLM 在我们 Mesh 上的切分方案和带宽需求。

## Ironwood / TPU v7（HC37）

- 首个双计算 die 的 TPU，FP8 4614 TFLOPS；8 堆 HBM3E，192 GiB，7.3 TB/s；1.2 TB/s 芯片间 I/O（p.14）。
- 安全：集成信任根（iROT）；硬件支持安全启动、安全测试与调试；**PCIe 支持 DOE（数据对象交换）和 CMA（组件度量与认证）**（p.21）。
- 电源：大规模训练时功耗剧烈波动，用软硬件结合的方式做功耗平滑（p.12）；有动态调频调压。
- 每颗芯片用 PCIe Gen5 x16 连接 host（p.22）。

## 对我们的启示

| 我们的设计问题 | 借鉴点 | 对应文档 |
|---|---|---|
| Host 下发指令的粒度 | v1 由 host 逐条下发 CISC 指令；对 LLM 而言 PCIe 往返太多，应由设备端固件派发 | SPEC-400 / 330 |
| 带宽 | v1 自己就受带宽限制，换 GDDR5 性能约 3 倍；再次说明 LLM 的 KPI 由 DRAM 带宽决定 | DOC-001 / 002 |
| 矩阵单元大小 | 从 v1 的 256×256 缩到 v2 的 128×128，多个阵列并列；对 batch=1 decode，大阵列利用率更差 | SPEC-210 |
| 向量单元 | v2 用通用向量单元加统一向量存储，取代固定功能的激活流水线；兼顾 M100 的可配置流水线思路 | SPEC-220 / 200 |
| 性能计数器 | v1 的陷阱"计数器事后再补"；我们要在 SPEC 阶段就定义好性能计数器事件 | SPEC-350，DOC-004 |
| 同步 | v2 的 DMA 完成时置同步标志，执行单元可以等待标志，与计数器同步一致 | SPEC-250 |
| 数值 | bf16 乘法器面积约为 fp16 的一半；激活值用 bf16 还是 fp16，要在 SPEC-240 里权衡 | SPEC-240 |
| 解析模型 | POPE 的成本模型和切分框架直接用于 DOC-002 | DOC-002 |
| 安全 | 信任根、安全启动、安全调试；PCIe DOE/CMA 设备认证 | SPEC-330 / 310 |

## 存疑与待核对

- HC32 和 HC37 讲稿的大量信息在框图里，文字提取不全；转换出的 Markdown 每页都附有整页图，看图即可，极小的字再回 PDF 原页。
