# Qwen3-0.6B 映射报告（man-qwen3-mapping）

| 项 | 内容 |
|---|---|
| 版本 | v0.1 |
| 状态 | 草稿（Draft） |
| 负责 | 架构线 |
| 读者 | 架构线、编译器团队、kernel 作者；Compute Tile、NoC、DRAM 规格的作者 |

---

## 1. 范围

本报告从 Qwen3-0.6B 的官方结构参数出发，推导一层的算子形状、权重与 KV cache 的容量、decode 与 prefill 的访存和算力需求、16 个 Compute Tile 的切分方式，以及第一个端到端测试（`e2e_gemm`）选用的形状。

- 本报告是推算报告，不是规格：硬件参数（Tile 数、SRAM 容量、矩阵引擎的形状、设备内存带宽、NoC 链路宽度、数值规则）只引用拥有它们的规格，不在这里定义（§8）。规格修改参数后，按 §9 的公式重算本报告。
- 除注明出处的以外，数字都是推算。容量用二进制前缀（1 MiB = 2²⁰ B），带宽与模型大小用十进制（1 GB = 10⁹ B）。
- 只覆盖 INT8 存储的权重与激活。量化的缩放因子、校准方法和浮点算子（RMSNorm、RoPE、SiLU、softmax）不在本报告范围内。
- `spec-arch` 的 `workload.primary` 仍是 LLaMA2-7B；本报告按本轮的目标模型 Qwen3-0.6B 分析，顶层的负载参数待 `spec-arch` 同步修改，本报告不代替那次修改。

## 2. 模型参数的来源

| 项 | 值 |
|---|---|
| 仓库 | Hugging Face `Qwen/Qwen3-0.6B`：<https://huggingface.co/Qwen/Qwen3-0.6B/blob/c1899de289a04d12100db370d81485cdf75e47ca/config.json> |
| revision | `c1899de289a04d12100db370d81485cdf75e47ca`（lastModified 2025-07-26T03:46:27Z） |
| `config.json` 的 SHA-256 | `660db3b73d788119c04535e48cf9be5f55bc3100841a718637ae695b442f27dd` |
| 结构的实现 | transformers v4.51.0（`config.json` 的 `transformers_version`）的 `src/transformers/models/qwen3/modeling_qwen3.py`，SHA-256 `704c914530530a1acb0b443add1f520404e3ac2c28c0ab7e16f80f86cfe8ccb2`：<https://github.com/huggingface/transformers/blob/v4.51.0/src/transformers/models/qwen3/modeling_qwen3.py> |
| 模型卡 | 同一 revision 的 `README.md`（参数个数、层数、头数、上下文长度 32,768） |
| 获取日期 | 2026-11-04 |

本报告用到的 `config.json` 字段：

| 字段 | 值 | 记号 |
|---|---:|---|
| `hidden_size` | 1024 | H |
| `intermediate_size` | 3072 | I |
| `num_hidden_layers` | 28 | L |
| `num_attention_heads` | 16 | n_q |
| `num_key_value_heads` | 8 | n_kv |
| `head_dim` | 128 | d |
| `vocab_size` | 151936 | V |
| `tie_word_embeddings` | true | lm_head 与 embedding 共用一份权重 |
| `attention_bias` | false | 线性层都没有偏置 |
| `hidden_act` | silu | |
| `max_position_embeddings` | 40960 | 模型卡写的上下文长度为 32,768 |
| `torch_dtype` | bfloat16 | 发布的权重格式 |

结构（`modeling_qwen3.py`）：每层是两段带残差的计算，`u = x + Attention(RMSNorm(x))`，`y = u + MLP(RMSNorm(u))`。

- 注意力：q、k、v 三个投影 → q、k 各按头做 RMSNorm（`q_norm`、`k_norm`，沿每个头的 d = 128 维归一化，每层两组长度为 d 的共享权重）→ RoPE（`rotate_half` 把每个头的前后两半配对）→ 写入 KV cache → GQA，每 n_q / n_kv = 2 个查询头共用一个 KV 头 → o_proj。
- MLP：`down_proj(silu(gate_proj(h)) × up_proj(h))`。
- 全部层之后是 RMSNorm 和 lm_head。

核对：按 §3 推算总参数 596,049,920 个，不含 embedding 的 440,467,456 个，与模型卡的"0.6B、Non-Embedding 0.44B"一致。

## 3. 一层的线性层

记号 `A[M, K] × B[K, N] = C[M, N]`：M 是本次处理的 token 数（decode 为 1，prefill 为分块大小），B 是权重，按数学上的 `[K, N]` 布局书写（PyTorch 的 `nn.Linear` 把它存成 `[N, K]`）。

| 算子 | K | N | 公式 | 参数个数 |
|---|---:|---:|---|---:|
| q_proj | 1024 | 2048 | K = H，N = n_q × d | 2,097,152 |
| k_proj | 1024 | 1024 | K = H，N = n_kv × d | 1,048,576 |
| v_proj | 1024 | 1024 | K = H，N = n_kv × d | 1,048,576 |
| o_proj | 2048 | 1024 | K = n_q × d，N = H | 2,097,152 |
| gate_proj | 1024 | 3072 | K = H，N = I | 3,145,728 |
| up_proj | 1024 | 3072 | K = H，N = I | 3,145,728 |
| down_proj | 3072 | 1024 | K = I，N = H | 3,145,728 |
| 合计 | | | | 15,728,640 |

q_proj 的输出是 n_q × d = 2048，不是 H = 1024；o_proj 的输入相应是 2048。

注意力的两次矩阵乘（每层、每个查询头）：分数 `Q[M, d] × Kᵀ[d, c]`，加权 `P[M, c] × V[c, d]`，c 是矩阵中 K、V 的序列长度；每一行实际可见的长度由 causal mask 决定。

- decode（一个 token、可见长度 c）：每层 2 × n_q × d × c = 4096c 次乘加，28 层 114,688c；c = 4096 时约 4.70 × 10⁸ 次，与全部线性层相当（§5.2）。
- causal prefill：块前已有 p 个 token、本块 M 个 token，只计有效的（未被 mask 的）配对时，整块为 2 × L × n_q × d × [M × p + M(M + 1)/2] 次；按矩形稠密执行时更多。

本轮的端到端测试不包含注意力。

## 4. 容量（INT8 存储）

| 项 | 公式 | 字节数 |
|---|---|---:|
| 每层线性层 | §3 合计 × 1 B | 15,728,640（15 MiB） |
| 28 层线性层 | × L | 440,401,920（420 MiB） |
| embedding = lm_head | V × H | 155,582,464（148.375 MiB），共用只计一份 |
| 合计 | | 595,984,384（568.375 MiB，约 596.0 MB） |
| RMSNorm 的权重 | L × (2H + 2d) + H = 65,536 个参数 | BF16 为 128 KiB，以下忽略 |

上表不含量化的缩放因子、对齐和打包开销。16 个 Compute Tile 的 SRAM 合计 32 MiB（`spec-arch` §3.2 的 `ct.sram_size` × 16），远小于 568 MiB，所以全部权重不能同时常驻 Compute Tile 的 SRAM。这不排除保留一部分权重、或多个序列共用一次权重读取的方案；§5.1 只估算其中最简单的流式方案。

**KV cache**：每个 token 每层 K、V 各 n_kv × d = 1024 个元素，28 层共 57,344 个。本报告只对 GEMM 采用 INT8 的分析假设，KV 的格式与完整的数值规则尚待相关规格确定，下表列两种情况：

| 上下文长度 c | INT8（56 KiB/token） | BF16（112 KiB/token） |
|---:|---:|---:|
| 2048 | 112 MiB | 224 MiB |
| 4096 | 224 MiB | 448 MiB |
| 32768 | 1792 MiB | 3584 MiB |

都在设备内存容量（`spec-arch` §3.3，32 GiB）之内。

## 5. 访存与算力

### 5.1 decode 的访存上限

按以下流式方案估算：单个序列、batch = 1；不在 decode 步之间保留权重，每步读一遍全部线性层与 lm_head 的权重（596.0 MB）；每步计算完整的 lm_head；每个 KV 元素每步从设备内存只读一次，由共用它的两个查询头复用（若两个 Tile 各自读取同一个 KV 头，KV 的流量相应增加）。以设备内存的理论峰值带宽 136.528 GB/s（`spec-arch` §3.3，引脚上的理想值）计：

| 情形 | 每 token 读取 | token/s 上限 |
|---|---:|---:|
| 只计权重 | 596.0 MB | 229 |
| c = 2048，KV 为 INT8 | 713.4 MB | 191 |
| c = 4096，KV 为 INT8 | 830.9 MB | 164 |
| c = 4096，KV 为 BF16 | 1065.7 MB | 128 |

未计 KV 的追加写入、激活、中间结果写回和协议开销。表中是这种方案下的上限，不是硬件的无条件吞吐上限；实际可用带宽由 `spec-dram`（待写）给出后重算。

### 5.2 算力与临界的 M

矩阵引擎的候选参数（`spec-arch` §5.2，参数待 `spec-matrix-engine`（待写）确认）：每个 Compute Tile 2048 次乘加/周期（8 × 64 阵列、每单元 4 元素点积），16 个 Tile 在 1 GHz 下共 32.8 × 10¹² 次/s。

- 线性层与 lm_head 每个 token 共 595,984,384 次乘加（每个权重一次）。
- **decode（M = 1）**：若 M 映射到阵列的 8 行，最多用到 1/8，16 个 Tile 约 4.1 × 10¹² 次/s；按这个吞吐，线性层的理想计算时间约 0.15 ms，而读权重需要约 4.37 ms。在 §5.1 的流式方案和理想计算吞吐的假设下，权重读取的时间占主导，约为计算的 30 倍。
- **临界的 M**：每个权重字节在一次分块中被复用 M 次。对 INT8 权重、以权重流量为主、阵列被充分利用的 GEMM，M 超过峰值算力 ÷ 带宽 = 32.8 × 10¹² ÷ 136.528 × 10⁹ ≈ 240 才转为受算力限制（roofline 的交点）。它不计激活、KV、注意力和非线性算子，不是整个模型 prefill 的统一转折点。
- **prefill（M = 32）**：按峰值吞吐，32 个 token 都计算完整的 lm_head 时，理想计算时间约 0.58 ms；只对块中最后一个 token 计算 logits 时约 0.43 ms。读权重仍要约 4.37 ms，在同样的假设下仍是权重读取占主导。以上都未计注意力、非线性算子、片上供数和同步。本轮选 M = 32 是为了覆盖多行的计算路径（§7），不是性能上的最优分块。
- **每个 Tile 的供数**：设备内存带宽平均到每个 Tile 约 8.5 GB/s（1 GHz 下约 8.5 B/周期）。阵列已在行、列方向上广播操作数、但不计跨周期的复用时，每周期要读 A 的 8 × 4 = 32 B 和 B 的 64 × 4 = 256 B，共 288 B。阵列内是否复用、SRAM 每周期能供多少字节、多个引擎怎样仲裁，都由 `spec-compute-tile`（待写）和 `spec-matrix-engine`（待写）回答；decode 时阵列大部分时间在等数据。

## 6. 16 个 Compute Tile 的切分

### 6.1 沿 N 切分线性层

每个 Tile 拿到 B 的 N / 16 列，用相同的 A（由一对多的分发提供给各 Tile，实现见 §6.2），算出 C 的 N / 16 列。沿 N 切分不切 K，每个 Tile 独立完成自己那一份的全部累加，不需要跨 Tile 归约。

| 算子 | 每 Tile 的 N | 每 Tile 的权重 |
|---|---:|---:|
| q_proj | 128 | 128 KiB |
| k_proj | 64 | 64 KiB |
| v_proj | 64 | 64 KiB |
| o_proj | 64 | 128 KiB |
| gate_proj | 192 | 192 KiB |
| up_proj | 192 | 192 KiB |
| down_proj | 64 | 192 KiB |
| 每层合计 | | 960 KiB |

一层的权重平均到每个 Tile 是 960 KiB，28 层是 26.25 MiB，不能常驻（§4）。

### 6.2 层内的数据依赖

沿 N 切分后，每个 Tile 只持有输出向量的一段。下表列出逻辑上的依赖；用汇集（all-gather）、归约还是多播实现，由 `spec-noc`（待写）和 `spec-tile-sync`（待写）决定，表中的做法只是可选方案之一。

| 位置 | 每个 Tile 持有 | 依赖 |
|---|---|---|
| q 的 QK-Norm、RoPE | q_proj 的 128 列，正好是一个完整的查询头 | 都在本 Tile 内完成 |
| k 的 QK-Norm、RoPE | k_proj 的 64 列，是半个 KV 头 | 按源码顺序（投影 → QK-Norm → RoPE → 写 KV cache → 注意力），K-Norm 需要整个头 128 维的平方和，RoPE 的 `rotate_half` 又把前后两半配对，两者都跨越这两个 Tile。要么先交换原始的半个头，要么先归约平方和、再交换做 RoPE 所需的另一半；也可以改为按 KV 头切分 k、v，由两个 Tile 重复计算 |
| 注意力 | — | 查询头 2j、2j + 1 共用 KV 头 j 的完整 K、V |
| o_proj 之前 | 一个头的注意力输出 | o_proj 沿 N 切分，每个 Tile 都要全部 16 个头的输出（K = 2048） |
| 两次残差加 | 输出向量的一段 | 按分片在本地完成，前提是对应的残差输入分片留在同一个 Tile、没有被之前的计算覆写 |
| RMSNorm | 输出向量的一段 | 需要全部 H = 1024 维的平方和：可以只归约这个统计量，再各自归一化 |
| 下一个投影之前 | 归一化后的一段 | 沿 N 切分的投影要完整的输入向量 |
| down_proj 之前 | `silu(gate) × up` 的 192 维 | 要完整的 I = 3072 维中间结果 |

本轮的端到端测试只做一个线性层：不涉及层内中间结果的跨 Tile 汇集或归约，但涉及把输入 A 分发给全部 16 个 Tile，以及分发完成后的同步。分发用 NoC 的硬件多播还是多次单播，由 `spec-noc`（待写）决定；`spec-arch` 中关于多播的描述针对的是指令派发，不能当作张量数据多播已有定义的依据。测试要写明它验证的是哪一条路径。

### 6.3 SRAM 中同时存活的数据

SRAM 按同时存活的数据做预算，不按模型大小除以 Tile 数。以 q_proj 为例（每 Tile 的 N = 128，K = 1024）：

| 数据 | M = 1 | M = 32 |
|---|---:|---:|
| B 的切片（INT8，K × 128） | 128 KiB | 128 KiB |
| A（INT8，M × K） | 1 KiB | 32 KiB |
| C（INT32，M × 128） | 512 B | 16 KiB |
| 向量加的另一个操作数（INT32，M × 128） | 512 B | 16 KiB |
| 合计（全部常驻、不分块，D 原地覆写 C） | 130 KiB | 192 KiB |
| 合计（D 单独存放） | 130.5 KiB | 208 KiB |

分块的例子：Mb = 32、Nb = 128、Kb = 256 时，A 块 8 KiB、B 块 32 KiB；A、B 双缓冲共 80 KiB，加上 INT32 的 C 块 16 KiB，共 96 KiB。这只是 GEMM 的缓冲；保持它们存活再加入 E 是 112 KiB，再加单独的 D 是 128 KiB。若 E、D 复用已经释放的 A、B 缓冲，要另行说明时序与布局。

在上述缓冲假设下，本测试的数据工作集远小于当前的 `ct.sram_size`（2 MiB）。这不构成全模型的 SRAM 定容结论：容量由 `spec-compute-tile`（待写）按完整的映射确定，控制核的指令与数据存储、ECC 位、DMA 描述符等的开销也在那里单独列出，不计入上表。

## 7. 本轮端到端测试选用的形状

借用 Qwen3 q_proj 的矩阵形状构造一个 GEMM 加向量加的硬件测试。Qwen3 并不在 q_proj 之后接 INT32 的向量加（模型的残差在 o_proj 和 down_proj 之后，§2）；E 是合成的测试操作数，不代表模型的残差或偏置。

| 项 | 选择 |
|---|---|
| 算子 | `A[M, 1024] × B[1024, 2048]`，有符号 INT8 输入，INT32 累加与输出；然后是向量加 `D = C + E`，E 是 INT32 的 `[M, 2048]` |
| 切分 | 沿 N 分到 16 个 Tile，每 Tile 的 N = 128；A 分发到全部 Tile（§6.2）；不切 K，不需要跨 Tile 归约 |
| M | 先 M = 1（decode），再 M = 32（一个 prefill 块） |
| host 搬入 | B 2 MiB；A 1 KiB（M = 1）或 32 KiB（M = 32）；E 8 KiB 或 256 KiB |
| host 搬回 | D 8 KiB 或 256 KiB |
| 判定 | D 与 host 参考实现按位一致 |
| 溢出 | INT8 输入时 \|C\| ≤ 1024 × 128 × 128 = 2²⁴，GEMM 本身不会溢出 INT32；在 `spec-numerics`（待写）定义溢出规则之前，E 取 \|E\| < 2³¹ − 2²⁴，保证 C + E 不溢出。溢出行为另行测试 |

按位一致只验证所选的整数运算，不验证与 BF16 模型的等价性、量化精度，也不验证后续算子的输入格式。

要声称覆盖下列路径，测试必须固定相应的条件，不能只靠形状：

- **跨 K 分块的累加**：kernel 的 Kb 要小于 1024，并确认每个 K 块都被执行。本轮采用 Kb = 256，K = 1024 恰好分为 4 块；
- **经设备内存取数**：每个 Tile 的 B 切片只有 128 KiB，放得进 SRAM；测试要规定 B 先由 HDMA 写入设备内存，再由各 Tile 的 DMA 搬入，不由 host 直接写入 Tile 的 SRAM，并有能观察到这条路径的检查点；
- **行方向不满**：若 M 映射到阵列的行，M = 1 只用 8 行中的 1 行，要说明填充或屏蔽的方式；
- **尾块**：按 Kb = 256 不覆盖 K 的尾块（其他 Kb 是否有尾块取决于它能否整除 K）；N 的尾块取决于 Nb，例如 gate_proj 每 Tile 的 N = 192，Nb = 128 时有 64 列的尾块，Nb = 64 时没有。gate_proj 可作为下一个形状。

覆盖范围：只声称覆盖这一个形状的整数 GEMM 与向量加，不声称完整的 Qwen3 推理。注意力、RMSNorm、RoPE、SiLU、softmax、lm_head 和层内的汇集都不在本轮范围内。

## 8. 参数的归属

本报告引用、不定义的参数：

| 参数 | 本报告用到的值 | 所属规格 |
|---|---|---|
| Compute Tile 的数量 `ct.count` | 16 | `spec-arch` §8 |
| 每 Tile 的 SRAM `ct.sram_size` | 2 MiB | `spec-arch` §8，`spec-compute-tile`（待写）写成后移交 |
| 矩阵引擎的形状与峰值 | 8 × 64、4 元素点积、2048 次乘加/周期 | `spec-arch` §5.2（候选），`spec-matrix-engine`（待写） |
| 工作频率 `clock.core_freq` | 1 GHz（推算所用） | `spec-clock-reset-power` §10 |
| 设备内存的理论峰值带宽 | 136.5 GB/s | `spec-arch` §3.3，`spec-dram`（待写） |
| INT8 / INT32 的运算规则 | 本轮测试选择有符号 INT8 输入、INT32 累加；规则尚无规格定义 | `spec-numerics`（待写） |

## 9. 复算

以下脚本从 §2 的 `config.json` 复算 §3、§4、§5 和 §6.1 的核心数字（带宽与算力取 §8 的值）；§6.3 的缓冲预算按表中的公式手算：

```python
import json

c = json.load(open("config.json"))
H, I, L = c["hidden_size"], c["intermediate_size"], c["num_hidden_layers"]
nq, nkv, d, V = (c["num_attention_heads"], c["num_key_value_heads"],
                 c["head_dim"], c["vocab_size"])
shapes = {"q_proj": (H, nq * d), "k_proj": (H, nkv * d),
          "v_proj": (H, nkv * d), "o_proj": (nq * d, H),
          "gate_proj": (H, I), "up_proj": (H, I), "down_proj": (I, H)}
per_layer = sum(k * n for k, n in shapes.values())        # 15,728,640
weights = per_layer * L + V * H                            # 595,984,384 B
kv_per_token = L * 2 * nkv * d                             # 57,344 个元素
bw = 8 * 16 * 8533e6 / 8                                   # 136.5 GB/s
peak = 16 * 2048 * 1e9                                     # 乘加/s
norm = L * (2 * H + 2 * d) + H                             # 65,536
print(per_layer, weights, kv_per_token, norm)
print("总参数:", weights + norm, "不含 embedding:", weights - V * H + norm)
for ctx, kv_bytes in ((0, 1), (2048, 1), (4096, 1), (4096, 2)):
    per_token = weights + ctx * kv_per_token * kv_bytes
    print("c =", ctx, "KV 字节:", kv_bytes, "token/s:", bw / per_token)
print("临界 M:", peak / bw)                                 # 240
print("decode 算力 ms:", weights / (peak / 8) * 1e3)        # 0.15
print("prefill M=32 算力 ms:", 32 * weights / peak * 1e3)   # 0.58
print("prefill 只算末 token logits ms:",
      (32 * (weights - V * H) + V * H) / peak * 1e3)        # 0.43
print("读权重 ms:", weights / bw * 1e3)                      # 4.37
for name, (k, n) in shapes.items():
    print(name, k, n, n // 16, k * (n // 16) // 1024, "KiB/Tile")
```
