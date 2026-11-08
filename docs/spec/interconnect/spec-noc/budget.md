# NoC G0 参考预算

档案摘要为 `84bb0b9c82f183d001381c17d85c5ef924dd52365c3b2cd41c8caa090be66b77`。数值均为参考设计推算。

## 资源

| 存储项目 | 字节 |
|---|---|
| router_input_data | 100352 |
| router_input_descriptors | 25088 |
| router_input_sideband | 980 |
| router_output_data | 6272 |
| router_output_sideband | 62 |
| router_pipeline_metadata | 3136 |
| router_credit_state | 507 |
| router_arbitration | 290 |
| NI_receive_data | 90112 |
| NI_receive_descriptors | 11264 |
| transaction_requests | 57344 |
| transaction_responses | 57344 |
| transaction_metadata | 14336 |
| parent_descriptors | 19456 |
| source_CDC_data | 4096 |
| source_CDC_metadata | 512 |
| source_CDC_pointers | 12 |
| protection_mirrors | 4608 |
| source_state | 736 |
| NI_control_and_counters | 5632 |
| compute_permission_regions | 2592 |
| 合计 | 404731 |

有效 NI 22 个，请求者 NI 18 个，输入端口 98 个。
数据子请求槽 352 个，总事务槽 448 个。
路由器间控制及数据线合计 39520 bit，本地端口 22880 bit。
源 CDC 跨域灰码指针合计 32 bit，DDR CDC 为 128 bit。
DDR 所属 FIFO 数据存储为 4096 B，另列于 Memory Tile 成本。
DDR FIFO 描述符参考预算为 2048 B，读写指针及同步为 48 B。

计数为显式存储和布线位数，综合面积、布线长度和功耗由后续实现核验。

## 无竞争最大距离预算

目标服务取 250 ns。源端等待、返回反压和竞争分别另记。

| 来源 | 报文 | 跳数 | 源 CDC ps | Mesh 与 NI ps | DDR CDC ps | 端到端 ps |
|---|---|---|---|---|---|---|
| COMPUTE | READ64 | 8 | 0 | 74000 | 15592 | 339592 |
| COMPUTE | WRITE64 | 8 | 0 | 74000 | 15592 | 339592 |
| COMPUTE | CONTROL64 | 8 | 0 | 74000 | 0 | 324000 |
| MGMT | READ64 | 8 | 18000 | 74000 | 15592 | 357592 |
| MGMT | WRITE64 | 8 | 18000 | 74000 | 15592 | 357592 |
| MGMT | CONTROL64 | 8 | 18000 | 74000 | 0 | 342000 |
| PCIE | READ64 | 8 | 30000 | 74000 | 15592 | 369592 |
| PCIE | WRITE64 | 8 | 30000 | 74000 | 15592 | 369592 |
| PCIE | CONTROL64 | 8 | 30000 | 74000 | 0 | 354000 |

## 带宽与资源驻留约束

| 项 | GB/s |
|---|---|
| raw_per_direction | 64.000000 |
| full_data_payload_per_link | 32.000000 |
| memory_boundary_payload | 128.000000 |
| PCIE_source_CDC_payload | 16.000000 |
| MGMT_source_CDC_payload | 32.000000 |
| dram_pin_ideal | 134.928000 |
| HDMA_context_aggregate_at_reference_RTT | 11.082491 |
| current_producer_aggregate_at_reference_RTT | 2.770623 |

完整 64 B 载荷占两拍，单链路载荷上界为 32 GB/s，四个 Memory Tile 边界合计 128 GB/s。
HDMA 槽位速率按 64 个子请求槽和固定参考驻留时间推算，两个方向共享这些槽。
当前 HDMA 默认每次只发 64 B，现有每来源 16 个父事务额度会进一步限制为 16 个子请求在途。
表中同时列出这两种驻留示例，64 个 NoC 槽的接入效果由 G2 验证。
参考驻留时间取八跳相位包络，N×64 B/T 仅用于固定驻留敏感性分析。实际吞吐上界需要实际平均驻留时间或独立证明的最小驻留约束。
父事务大小、分段并行度、控制流量、反压和目标服务还会影响实际吞吐。

## 当前 DRAM 档案

| 参数 | 值 |
|---|---|
| CHANNELS | 8 |
| RANKS_PER_CHANNEL | 2 |
| TOTAL_BYTES | 34359738368 |
| BURST_BYTES | 64 |
| DATA_RATE_MT_S | 8433 |
| CK_PS | 949 |
| TRFC_PS | 280000 |
| TXS_PS | 287500 |
| TREFI_PS | 3900000 |
| READ_SLOTS | 64 |
| WRITE_SLOTS | 64 |
| FRONT_SLOTS | 64 |
| PIPELINE_CYCLES | 2 |

带 PS 后缀的参数单位为 ps。控制器前后端流水各为 PIPELINE_CYCLES 个 CK，计入目标服务。
原生分段 burst 的各参数分别量化，完整时序由 LPDDR5.py 及内存规格拥有。

## 低速参考

假设核心及来源均为 25 MHz，八跳 Mesh 读写往返为 2960000 ps，源端 CDC 为 480000 ps。
此行用于区分时钟档位，当前固件释放 NoC 的真实时序仍由时钟复位规格拥有。
运行中低速档和未初始化目标各自受准入条件约束。

## 验证层次

已检查 576 个源目的组合及四个独立 VN 的通道依赖图。
通道图分析与响应预留规则共同进入 G1 的协议进展验证。
原生 FR-FCFS 的目标服务最坏上界保持未证明状态。
G1 周期实测、G2 接入和 G3 整机性能各自保留独立关口。
