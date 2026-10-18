# 参考资料库

> 维护：架构/gem5 团队　·　建立 2026-10-17

本目录收集架构设计调研用的原始资料，分两批：

- 第一批：数据流架构代表，包括 Groq、Tenstorrent、理想马赫 M100、Google TPU。
- 第二批：端侧 NPU 竞品，包括 Hailo-10H、Axelera Metis/Europa、AMD XDNA，另附跨厂商评测。

调研结论和对我们设计的启示见 [SURVEY.md](SURVEY.md)；各家的设计精髓、经验教训，以及由此提炼的设计原则见 [LESSONS.md](LESSONS.md)。

## 使用须知

- **公开与版权**：本项目是开源的，这里的资料随仓库公开托管和分发（2026-10-18 确认）。资料的版权仍归原作者或出版方所有，引用时请注明出处。Hailo 的文档每页都标有"Confidential and Proprietary"，它们取自 Hailo 官网公开下载的版本，维护者已确认可以随仓库公开。
- **开源代码**：`hailo/hailort-drivers/` 是 GPL-2.0 许可的驱动源码，只供阅读参考。我们自己的驱动如果要复制其中的代码，必须先评估许可证的影响。
- **来源**：只收录官方或开放渠道的版本，包括 arXiv、厂商官网、Hot Chips 官方站点、官方 GitHub。官方链接已失效的，使用该官方链接在 Wayback Machine 上的存档。每条资料的具体来源见下表。
- **组织方式**：每份资料单独一个目录，以后可以在同一目录下补充阅读笔记。

## 资料的三层形态

| 层 | 文件 | 用途 | 注意 |
|---|---|---|---|
| 原件 | 每个资料目录里的 `*.pdf` | 引用依据；看图、看表 | 数字写进 SPEC 之前，以原件为准 |
| 自动转换 | 与 PDF 同名的 `*.md`，图片在同目录的 `images/` | 全文检索、按需阅读 | 由 docling 自动转换；`<!-- p.N -->` 标记对应 PDF 第 N 页，方便回原件核对；表格偶有错位，存疑时回 PDF 原页确认 |
| 阅读笔记 | 每个厂商目录下的 `NOTES.md` | 关键参数（带出处和页码）、架构要点、对我们 SPEC 的映射、待核对事项 | 人工整理；跨厂商的综合结论见 [SURVEY.md](SURVEY.md) |

图片的处理方式按文档类型区分：

- **论文、数据手册**（竖版页面）：只导出版面分析识别出的图片区域，插在 Markdown 中对应的位置。
- **讲稿**（横版页面）：框图常由矢量文本框拼成，识别不出完整的图片区域，所以每页导出一张整页图，放在该页的页码标记之后；页面上的文字仍照常提取，便于检索。
- 图片按 144 DPI 渲染。框图里很小的字可能看不清，需要时回 PDF 原页查看。
- 整页图默认存为 PNG；带照片的页面 PNG 体积很大，若 JPEG 不到 PNG 的 60% 就改存 JPEG（质量 90）。

本来就是 Markdown 或 rst 的资料（Tenstorrent 官方文档、Linux 内核文档、mlir-aie 编程指南）不做转换，直接阅读。

**转换工具**在仓库的 `tools/ref2md/`。新增 PDF 后，在仓库根目录执行：

```sh
# 第一次使用时建环境（约 1 GB，首次转换还会下载约几百 MB 的模型）
uv venv --python 3.12 tools/ref2md/.venv
uv pip install --python tools/ref2md/.venv/bin/python -r tools/ref2md/requirements.txt

# 只转换新增或更新过的 PDF
tools/ref2md/.venv/bin/python tools/ref2md/convert.py
```

## 目录结构

```
reference/
├── README.md        # 本文件：资料索引
├── SURVEY.md        # 调研综述（跨厂商的结论与启示）
├── LESSONS.md       # 设计精髓与经验（各家为什么这样做、学到了什么、我们的设计原则）
├── <厂商>/NOTES.md  # 各厂商的阅读笔记
├── groq/            # 第一批
├── tenstorrent/
├── li-auto-m100/
├── google-tpu/
├── hailo/           # 第二批
├── axelera/
├── amd-xdna/
└── cross-vendor/    # 跨厂商评测
```

目录命名规则：`<厂商>/<年份>-<会议或渠道>-<短名>/`。

---

## 资料清单

### Groq

| 目录 | 资料 | 出处 | 类型 | 获取来源 |
|---|---|---|---|---|
| `groq/2020-isca-tsp-think-fast/` | Think Fast: A Tensor Streaming Processor (TSP) for Accelerating Deep Learning Workloads | ISCA 2020 | 论文，14 页 | Groq 官网原链接（已下线）的 Wayback 存档 |
| `groq/2022-isca-tsp-multiprocessor/` | A Software-defined Tensor Streaming Multiprocessor for Large-scale Machine Learning | ISCA 2022 | 论文，14 页 | cdn.sanity.io 上的副本。托管方未能核实，但内容已核对为 ACM 正式版（DOI 10.1145/3470496.3527405） |
| `groq/2022-hotchips34-scaleout-tsp/` | The Groq Software-defined Scale-out Tensor Streaming Multiprocessor | Hot Chips 34（2022） | 讲稿，69 页 | hc34.hotchips.org |
| `groq/2022-atpesc-groq-ling/` | Groq 软件定义硬件讲座（Andrew Ling） | ATPESC 2022 | 讲稿，44 页 | 阿贡国家实验室 ATPESC 原链接的 Wayback 存档 |

### Tenstorrent

| 目录 | 资料 | 出处 | 类型 | 获取来源 |
|---|---|---|---|---|
| `tenstorrent/2020-hotchips32-compute-substrate/` | Compute Substrate for Software 2.0（Grayskull / Wormhole） | Hot Chips 32（2020） | 讲稿，31 页 | hc32.hotchips.org |
| `tenstorrent/2024-hotchips36-blackhole-metalium/` | Blackhole & TT-Metalium: The Standalone AI Computer and its Programming Model | Hot Chips 36（2024） | 讲稿，30 页 | hc2024.hotchips.org |
| `tenstorrent/tt-isa-documentation/` | Tenstorrent ISA 官方文档：Wormhole B0 / Blackhole A0 的 Tensix、NoC、PCIe Tile、DRAM Tile 等 | 官方 GitHub | 文档 | github.com/tenstorrent/tt-isa-documentation，commit `8a34e25`（见目录内 `SOURCE_COMMIT`） |
| `tenstorrent/tt-metal-tech-reports/` | TT-Metalium 技术报告选集，共 16 篇：矩阵引擎、数据格式、特殊浮点值、内存、张量布局与分片、DRAM/PCIe 带宽、GEMM、性能计数器、LLM、FlashAttention 等 | 官方 GitHub | 文档 | github.com/tenstorrent/tt-metal 的 `tech_reports/`，commit `a5eb1d9`；未收录多芯片扩展、CNN/ViT 模型适配和工具类报告 |
| `tenstorrent/2025-arxiv-riscv-matmul-assessment/` | Assessing Tenstorrent's RISC-V MatMul Acceleration Capabilities（第三方对 Grayskull 的评测） | arXiv 2505.06085 | 论文，12 页 | arXiv |
| `tenstorrent/2025-hpcasia-epcc-intro/` | Introduction to Tenstorrent（EPCC 教程讲稿） | HPC Asia 2025 | 讲稿，19 页 | riscv.epcc.ed.ac.uk |

### 理想汽车马赫 M100

| 目录 | 资料 | 出处 | 类型 | 获取来源 |
|---|---|---|---|---|
| `li-auto-m100/2026-isca-m100-orchestrated-dataflow/` | M100: An Orchestrated Dataflow Architecture Powering General AI Computing | ISCA 2026 Industry Track | 论文，12 页 | arXiv 2604.17862 |

M100 目前公开的技术资料只有这一篇论文。其余为新闻报道，以下链接仅作背景参考，内容多为厂商宣传口径：

- IT之家：[李想官宣公司自研马赫 M100 芯片](https://www.ithome.com/0/949/393.htm)（2026-05）
- 新浪科技：[理想汽车登台 ISCA 2026，发表马赫 M100 芯片论文](https://finance.sina.com.cn/tech/digi/2026-07-01/doc-inifihns6220591.shtml)（2026-07）
- Synopsys：[Li Auto 案例研究](https://www.synopsys.com/blogs/chip-design/synopsys-li-auto-case-study.html)，内容为使用 HAPS-100 做 SoC 与 NPU 原型验证

### Google TPU

| 目录 | 资料 | 出处 | 类型 | 获取来源 |
|---|---|---|---|---|
| `google-tpu/2017-isca-tpuv1/` | In-Datacenter Performance Analysis of a Tensor Processing Unit（TPUv1） | ISCA 2017 | 论文，17 页 | arXiv 1704.04760 |
| `google-tpu/2020-cacm-tpuv2v3-supercomputer/` | A Domain-Specific Supercomputer for Training Deep Neural Networks（TPUv2/v3） | CACM 2020 | 论文，12 页 | ACM DL 原链接的 Wayback 存档（DOI 10.1145/3360307） |
| `google-tpu/2020-hotchips32-tpuv2v3/` | Google's Training Chips Revealed: TPUv2 and TPUv3 | Hot Chips 32（2020） | 讲稿，70 页 | hc32.hotchips.org |
| `google-tpu/2021-isca-tpuv4i-ten-lessons/` | Ten Lessons From Three Generations Shaped Google's TPUv4i（推理专用 TPU 及十条经验） | ISCA 2021 | 论文，14 页 | 合著者 Mark Gottscho 的个人主页（markgottscho.com），已核对为 ISCA 正式版 |
| `google-tpu/2022-patterson-tpu-lessons-talk/` | A Decade of Machine Learning Accelerators: Lessons Learned and Carbon Footprint（Patterson） | MLSys 2022 研讨会 | 讲稿，84 页 | chips-compilers-mlsys-22.github.io |
| `google-tpu/2023-isca-tpuv4/` | TPU v4: An Optically Reconfigurable Supercomputer for Machine Learning with Hardware Support for Embeddings | ISCA 2023 | 论文，14 页 | arXiv 2304.01433 |
| `google-tpu/2023-mlsys-scaling-transformer-inference/` | Efficiently Scaling Transformer Inference（TPU v4 上的 LLM 推理切分与解析模型） | MLSys 2023 | 论文，18 页 | arXiv 2211.05102 |
| `google-tpu/2025-hotchips37-ironwood/` | Ironwood: Delivering Best in Class perf, perf/TCO and perf/Watt for Reasoning Model Training and Serving（TPU v7） | Hot Chips 37（2025） | 讲稿，26 页 | hc2025.hotchips.org |

### Hailo（第二批）

| 目录 | 资料 | 出处 | 类型 | 获取来源 |
|---|---|---|---|---|
| `hailo/2026-hailo10h-product-brief/` | Hailo-10H AI Accelerator 产品简介 | Hailo 官网（2025-07 版） | 产品简介，2 页 | hailo.ai 官方链接的 Wayback 存档（2026-06）。官网有反爬拦截，无法直接下载 |
| `hailo/2026-hailo10h-m2-product-brief/` | Hailo-10H M.2 Key M ET 模块产品简介 | Hailo 官网（2025-07 版） | 产品简介，2 页 | 同上 |
| `hailo/2026-hailo8-datasheet/` | Hailo-8 Datasheet Rev 1.9.2（包含 NN Core、PCIe 端点、启动方式、MCU 的说明） | Hailo 官网（2026-04 版） | 数据手册，42 页 | 同上（2026-10 快照） |
| `hailo/hailort-drivers/` | Hailo PCIe 驱动源码（支持 Hailo-10/15）：BAR 布局、vDMA 描述符、固件加载与控制协议 | 官方 GitHub | 源码，GPL-2.0 | github.com/hailo-ai/hailort-drivers，固定 commit 见 `SOURCE_COMMIT` |

### Axelera（第二批）

| 目录 | 资料 | 出处 | 类型 | 获取来源 |
|---|---|---|---|---|
| `axelera/2024-edge-ai-summit-verhoef-dimc/` | How Axelera AI Uses Digital Compute-in-Memory（Metis 与 D-IMC 原理） | Embedded Vision Summit 2024 | 讲稿，20 页 | edge-ai-vision.com |
| `axelera/2025-hpcasia-epcc-axelera/` | Revolutionizing AI Workloads with D-IMC and RISC-V（Metis SoC 与 AI Core 架构、规格表） | HPC Asia 2025 | 讲稿，16 页 | riscv.epcc.ed.ac.uk |
| `axelera/2025-esa-workshop-axelera/` | Datacenter Performance. Edge Efficiency.（公司与产品路线：Metis、Europa、Titania） | 欧空局（ESA）研讨会 | 讲稿，15 页 | indico.esa.int |
| `axelera/2022-esscirc-eleftheriou-imc/` | In-memory computing for deep-learning acceleration（Axelera CTO，存内计算综述） | ESSCIRC 2022 | 讲稿，30 页 | axelera.ai 官网 |

Europa 目前没有技术文档，只有新闻稿：[Axelera launches Europa AIPU](https://axelera.ai/news/axelera-launches-europa-aipu-setting-new-industry-benchmark-for-ai-accelerator-performance-power-efficiency-and-affordability)（2025-10）。

### AMD XDNA（第二批）

| 目录 | 资料 | 出处 | 类型 | 获取来源 |
|---|---|---|---|---|
| `amd-xdna/2023-hotchips35-ryzen7040-phoenix/` | AMD Ryzen 7040 Series（Phoenix）SoC，包括第一代 XDNA NPU | Hot Chips 35（2023） | 讲稿，27 页 | hc2023.hotchips.org |
| `amd-xdna/linux-kernel-amdxdna-doc/` | Linux 内核 amdxdna 驱动文档：NPU 硬件组成、固件、mailbox、BAR、PASID 隔离、空间与时间混合调度、启动与错误处理 | Linux 内核 `Documentation/accel/amdxdna/` | 文档 | github.com/torvalds/linux，固定 commit 见 `SOURCE_COMMIT` |
| `amd-xdna/mlir-aie-programming-guide/` | IRON / mlir-aie 编程指南：Tile 类型、DMA 与 BD、ObjectFIFO、同步锁、路由 | 官方 GitHub | 文档，含示例代码 | github.com/Xilinx/mlir-aie 的 `programming_guide/`，commit `0094b28` |
| `amd-xdna/2026-mlarchsys-llm-on-xdna2/` | From Human Guidance to Autonomy: Agent Skill System for End-to-End LLM Deployment on Spatial NPUs（在 XDNA2 上端到端部署 LLM） | ISCA 2026 MLArchSys 研讨会 | 论文，6 页 | arXiv 2606.07586 |
| `amd-xdna/2025-arxiv-ryzen-ai-gemm-generations/` | Striking the Balance: GEMM Performance Optimization Across Generations of Ryzen AI NPUs（XDNA 与 XDNA2 的架构对比与 GEMM 实测） | arXiv 2512.13282 | 论文，12 页 | arXiv |
| `amd-xdna/2025-arxiv-npu-bare-metal-training/` | Unlocking the AMD NPU for ML Training on the Client Using Bare-Metal-Programming Tools | arXiv 2504.03083 | 论文，10 页 | arXiv |

### 跨厂商评测

| 目录 | 资料 | 出处 | 类型 | 获取来源 |
|---|---|---|---|---|
| `cross-vendor/2026-arxiv-npu-hardware-evaluation/` | NPU Hardware Evaluation v1.0：10 款端侧加速器的规格与实测，包括 Hailo-8/10H、Axelera Metis/Europa、AMD XDNA2 | arXiv 2609.13166 | 论文，14 页 | arXiv |
| `cross-vendor/2026-arxiv-llm-edge-sustained-load/` | LLM Inference at the Edge: Mobile, NPU, and GPU Performance Efficiency Trade-offs Under Sustained Load（含 Hailo-10H 的 LLM 实测） | arXiv 2603.23640 | 论文，15 页 | arXiv |

---

## 未收录的资料

以下资料没有拿到。如果公司有相应订阅，或者在可以访问这些站点的网络环境下，可以下载后放进对应目录。

| 资料 | 出处 | 说明 |
|---|---|---|
| The Design Process for Google's Training Chips: TPUv2 and TPUv3 | IEEE Micro 41(2), 2021 | 需要 IEEE 订阅；内容与 HC32 讲稿高度重合 |
| Compute Substrate for Software 2.0（Tenstorrent） | IEEE Micro 41(2), 2021 | 需要 IEEE 订阅；内容与 HC32 讲稿对应 |
| AMD XDNA NPU in Ryzen AI Processors（Rico 等） | IEEE Micro, 2024 | 需要 IEEE 订阅；是 XDNA 最权威的架构论文 |
| Versal AIE-ML Architecture Manual（AM020）与 AIE-ML v2 Architecture Manual（AM027） | docs.amd.com | XDNA 与 XDNA2 计算阵列的官方架构手册，**建议优先补**。docs.amd.com 从当前网络连不上，Wayback 也没有 PDF 存档。在可访问的网络下，用网页上的导出功能存为 PDF |

另外，dl.acm.org、cacm.acm.org、docs.amd.com 在当前网络环境下都无法访问，直连和走代理都在 TLS 握手阶段被重置。所以 ACM 的论文改用了上表列出的替代来源，Hailo 的文档则改用 Wayback 存档。

## 新增资料的方式

1. 按命名规则新建一个空目录，例如 `google-tpu/2021-isca-tpuv4i-ten-lessons/`。
2. 优先从官方或开放渠道下载，在本文件的清单里补一行，写明来源。
3. GitHub 文档按固定 commit 下载，把 commit 号写进目录内的 `SOURCE_COMMIT`，不要带 `.git` 目录。
