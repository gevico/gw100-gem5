# PCIe 链路来源与覆盖矩阵

<!-- 由 tools/pcie_spec.py 生成，请勿手改 -->

档案版本为 0.1。gw100-link.json SHA-256=c29b2defc6b3efc431d314589de4824b7cfd63e778c23721743321dd40a4dfea

## 实施关口

| 关口 | 当前状态 |
|---|---|
| G0 | complete |
| G1 | pending |
| G2 | waiting_for_G1 |
| G3 | waiting_for_G2 |

当前表区分来源核验、参考设计和实现验收。
共享定义覆盖已核验常量和已批准预算，后续链路实现依关口推进。

## 来源版本

### SIG-CATALOG

PCI-SIG Specifications，online catalogue。
发布标记为 在线 FAQ 或目录，核验日期为 2026-11-07。

定位为 Purchasing Specifications。公开目录说明规范获取方式，目录本身提供书目信息。 [原始来源](https://pcisig.com/specifications)

### PG213-STATUS

UltraScale+ Devices Integrated Block for PCI Express，PG213 v1.3。
发布标记为 2026-07-31，核验日期为 2026-11-07。

定位为 Configuration Status Interface。核对四代速率、MPS/MRRS 编码、RCB 和 LTSSM 状态观测接口。 [原始来源](https://docs.amd.com/r/en-US/pg213-pcie4-ultrascale-plus/Configuration-Status-Interface)

### PG213-FC

UltraScale+ Devices Integrated Block for PCI Express，PG213 v1.3。
发布标记为 2026-07-31，核验日期为 2026-11-07。

定位为 Configuration Flow Control Interface。核对六类信用的观测字段及不同观测模式的无限信用表示。 [原始来源](https://docs.amd.com/r/en-US/pg213-pcie4-ultrascale-plus/Configuration-Flow-Control-Interface)

### ALTERA-FC

AXI Streaming IP for PCI Express User Guide，26.1。
发布标记为 2026-10-23，核验日期为 2026-11-07。

定位为 6.3.8 Flow Control Credit Handling。核对 P/NP/CPL 头与数据分别计量，数据信用的单位为 16 B。 [原始来源](https://docs.altera.com/r/docs/790711/26.1/axi-streaming-ip-for-pci-express-user-guide/flow-control-credit-handling)

### PG213-RCB

UltraScale+ Devices Integrated Block for PCI Express，PG213 v1.3。
发布标记为 2026-07-31，核验日期为 2026-11-07。

定位为 Read Completion Boundary。核对 RCB 64/128 B、完成拆分边界和最坏完成头资源预留。 [原始来源](https://docs.amd.com/r/en-US/pg213-pcie4-ultrascale-plus/Read-Completion-Boundary)

### PG213-CPL

UltraScale+ Devices Integrated Block for PCI Express，PG213 v1.3。
发布标记为 2026-07-31，核验日期为 2026-11-07。

定位为 Completer Completion Descriptor Fields。核对 Lower Address 与请求地址和 Byte Enable 的对应关系。描述符为厂商内部接口表示。 [原始来源](https://docs.amd.com/r/en-US/pg213-pcie4-ultrascale-plus/Completer-Completion-Descriptor-Format)

### AM012-LL

Versal Adaptive SoC Register Reference，AM012 revision 1.6。
发布标记为 2026-08-24，核验日期为 2026-11-07。

定位为 LL_ACK_TIMEOUT, LL_REPLAY_TIMEOUT, LL_REPLAY_TIMER40_ENABLE。核对超时覆盖接口及单位。Gen4 replay 定时使用独立规则，具体默认值仍指向内置表。 [原始来源](https://docs.amd.com/r/en-US/am012-versal-register-reference/CPM4_PCIE0_ATTR-Module?contentId=NawJTqKe66M8AtL47rdLZQ)

### PG054-LL

7 Series FPGAs Integrated Block for PCI Express，PG054 v3.3。
发布标记为 2024-12-06，核验日期为 2026-11-07。

定位为 ACK Latency Timer and Replay Timer。核对早期 IP 的内置表依赖链路宽度和 MPS，作用范围为该 IP 支持的代际。 [原始来源](https://docs.amd.com/r/en-US/pg054-7series-pcie/Link-Layer-Module)

### SIG-SKP

PCI-SIG Technical FAQ，PCI Express 3.0 category。
发布标记为 在线 FAQ 或目录，核验日期为 2026-11-07。

定位为 Section 4.2.7.3。核对 Gen3 SKP 调度周期与包边界推迟，调度周期和实际发送间隔分别计算。 [原始来源](https://pcisig.com/faq?keys=3.0&page=1)

### SIG-REPLAY

PCI-SIG Technical FAQ，PCI Express 3.0 category。
发布标记为 在线 FAQ 或目录，核验日期为 2026-11-07。

定位为 Section 3.5.2.1。核对 replay 超时后的重传与错误报告，该条 FAQ 提供行为说明。 [原始来源](https://pcisig.com/section-3521-if-you-can-receive-tlps-and-flow-control-dllps-normally-do-not-receive-any-ack-or-nak)

### LATTICE-X4

PCIe x4 IP Core User Guide，FPGA-IPUG-02126-2.5, IP v4.1.0。
发布标记为 2026-06，核验日期为 2026-11-07。

定位为 p.122 Table 3.4, p.212 Tables 5.87 and 5.88。已核对 PDF 原页。该 IP 的简化 replay 默认选项分别对应 24×1024 和 80×1024 符号时间。ACK 与 FC 推荐定时仍回指规范。该实例的配置值适用范围单独记录。 [原始来源](https://www.latticesemi.com/view_document?document_id=53091)

原件 SHA-256 为 `af417fffed9aad5717a7dfd9254041cc547acfaae5b135abeb85445ce6bc9fed`。

### BASE4-CB

PCI Express Base Specification Revision 4.0 Version 1.0，1.0-c, showing changes, cover 2017-09-27, history 2017-09-29。
发布标记为 2017-10-05，核验日期为 2026-11-07。

定位为 §2.2–2.8, §3.2–3.6, §4.2, §5.4, §6.2/6.6, §7.5/7.8。用户允许核验第三方托管的原版规范副本。正文与书目版本分别记录，参数按本副本页码引用。后续勘误单独列入核验范围。 [原始来源](https://raw.githubusercontent.com/WeitaoZhu/PCI_Express/273a7a47f0e1eb1df60bf5f711f6c4ec5cb94172/PCI_Express_Base_Specification/PCI_Express_Base_Specification_Revision4.0r1.0.pdf)

原件 SHA-256 为 `7d5c27b38f119459158c2b337052d4298ec53e12e89f11280b139726b830abdf`。

原版镜像由 PCI-SIG 编写，固定提交为 `273a7a47f0e1eb1df60bf5f711f6c4ec5cb94172`。
已检查封面和 p.6 版本历史。此件为修订标记稿，渲染页中蓝色删除文字须排除，红色新增文字纳入。摘要用于固定所读原件，官方未提供可供比对的公开摘要。 [官方书目](https://pcisig.com/PCIExpress/Specs/Base/_4.0_1.0)

## 协议规则档案

结构化规则与原文定位保存在同一共享档案的 `protocol_values` 中。

- `ack_replay_limits`。BASE4-CB pp.211–233 §3.6
- `framing_rules`。BASE4-CB pp.237–257,375–383 §4.2.1/2/7
- `crc_rules`。BASE4-CB pp.209,213–214,249–250
- `fc_state_machine`。BASE4-CB pp.169–175,198–208
- `wire_fields`。BASE4-CB pp.78–104,108–116,129–131,143–154,205–208
- `ordering_matrix`。BASE4-CB pp.155–157 Table 2-39
- `ltssm_rules`。BASE4-CB pp.301–359,374–375 §4.2.6
- `l1_rules`。BASE4-CB pp.366–367,476–483,1093 §4.2.6.7/5.4.1.2
- `configuration_reset_values`。BASE4-CB pp.767–775 §7.5.3.4/5
- `error_reset_rules`。BASE4-CB pp.153–155,545–564,584–590,860–877,903–918

## 共享预算

| 常量 | 数值 | 单位 | 依据 |
|---|---|---|---|
| DEFAULT_GENERATION | 4 | generation | 用户批准的首版链路档案 |
| DEFAULT_WIDTH | 4 | lanes | 用户批准的首版链路档案 |
| CONTROLLER_PERIOD_PS | 4000 | ps | 250 MHz 参考 RC/EP 时钟 |
| CONTROLLER_DATAPATH_BYTES | 32 | B/cycle | spec-clock-reset-power 的 256 位控制器数据通路 |
| REFERENCE_CLOCK_HZ | 100000000 | Hz | spec-pcie 的公共参考时钟设计 |
| TICK_PS | 1 | ps | 用户批准的全局 tick |
| ADAPTER_CYCLES | 2 | cycles | 用户批准的每侧适配流水 |
| ONE_WAY_PROPAGATION_PS | 5000 | ps | 用户批准的参考传播时延 |
| MAX_PAYLOAD_BYTES | 256 | B | 首版 EP 能力预算 |
| MAX_READ_REQUEST_BYTES | 512 | B | 首版 EP 请求上限 |
| MAX_OUTSTANDING_READS | 64 | requests | 首版请求账本容量 |
| COMPLETION_DATA_BYTES | 32768 | B | 64 × 512 B 完成数据预留 |
| MIN_RCB_BYTES | 64 | B | 首版支持的最小 RCB |
| MAX_RCB_BYTES | 128 | B | 首版支持的最大 RCB |
| COMPLETION_HEADER_SLOTS | 576 | headers | 64 × ceil((63 + 512) / 64) 的保守头部预留 |
| DATA_CREDIT_BYTES | 16 | B/credit | Altera 26.1 §6.3.8 |
| RX_PH | 32 | headers | 首版 posted 接收头预算 |
| RX_PD_BYTES | 2048 | B | 首版 posted 接收数据预算 |
| RX_NPH | 16 | headers | 首版 non-posted 接收头预算 |
| RX_NPD_BYTES | 256 | B | 16 项配置写的对齐数据信用预算 |
| REPLAY_DATA_BYTES | 4096 | B | 首版每侧 replay 数据预算 |
| REPLAY_ALIGNMENT_BYTES | 16 | B | 参考 replay 存储分配粒度 |
| REPLAY_DESCRIPTOR_SLOTS | 128 | descriptors | 参考 replay 元数据项数 |
| REPLAY_DESCRIPTOR_BYTES | 16 | B | 参考 replay 每项元数据预算 |
| TX_PH | 32 | headers | 参考待发 posted 头预算 |
| TX_PD_BYTES | 2048 | B | 参考待发 posted 数据预算 |
| TX_NPH | 16 | headers | 参考待发 non-posted 头预算 |
| TX_NPD_BYTES | 256 | B | 参考待发 non-posted 数据预算 |
| TX_CPLH | 16 | headers | 参考待发 completion 头预算 |
| TX_CPLD_BYTES | 4096 | B | 参考待发 completion 数据预算 |
| TX_DLLP_SLOTS | 16 | packets | 参考有限 DLLP 待发预算 |
| BACKEND_SLOTS | 64 | operations | 参考 RC/EP 后端操作预算 |
| RC_POSTED_RESERVED_SLOTS | 16 | operations | 参考 RC 在总计 64 槽中为 posted 保留 16 槽，NP 最多占 48 槽 |
| HOST_PACKET_SLOTS | 64 | packets | 参考 RC Host 内存包在途预算 |
| CREDIT_RELEASE_CYCLES | 2 | cycles | 用户批准的信用释放流水 |
| ACK_BATCH_TLPS | 8 | TLPs | 用户批准的累计 ACK 策略，协议期限由资料关口核验 |
| ASPM_L1_SUPPORTED | 1 | boolean | 用户批准的数字 L1 能力 |
| ASPM_L1_DEFAULT_ENABLED | 0 | boolean | 用户批准的默认 ASPM 策略 |
| BENCH_WARMUP_PS | 20000000 | ps | 参考流量预热 20 µs |
| BENCH_WINDOW_PS | 100000000 | ps | 用户批准的 100 µs 测量窗口 |
| BAR0_TARGET_PS | 500000 | ps | 用户批准的空队列单请求条件目标 |
| BAR2_TARGET_PS | 1000000 | ps | 用户批准的目标就绪单请求条件目标 |
| GEN1_BIT_HALF_PS | 800 | 0.5 ps | BASE4-CB §4.2 编码速率，推算 |
| GEN1_SYMBOL_PS | 4000 | ps | BASE4-CB §3.6 符号时间单位 |
| GEN1_MPS128_X1_ACK_SYMBOLS | 237 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN1_MPS128_X2_ACK_SYMBOLS | 128 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN1_MPS128_X4_ACK_SYMBOLS | 73 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN1_MPS256_X1_ACK_SYMBOLS | 416 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN1_MPS256_X2_ACK_SYMBOLS | 217 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN1_MPS256_X4_ACK_SYMBOLS | 118 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN2_BIT_HALF_PS | 400 | 0.5 ps | BASE4-CB §4.2 编码速率，推算 |
| GEN2_SYMBOL_PS | 2000 | ps | BASE4-CB §3.6 符号时间单位 |
| GEN2_MPS128_X1_ACK_SYMBOLS | 288 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN2_MPS128_X2_ACK_SYMBOLS | 179 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN2_MPS128_X4_ACK_SYMBOLS | 124 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN2_MPS256_X1_ACK_SYMBOLS | 467 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN2_MPS256_X2_ACK_SYMBOLS | 268 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN2_MPS256_X4_ACK_SYMBOLS | 169 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN3_BIT_HALF_PS | 250 | 0.5 ps | BASE4-CB §4.2 编码速率，推算 |
| GEN3_SYMBOL_PS | 1000 | ps | BASE4-CB §3.6 符号时间单位 |
| GEN3_MPS128_X1_ACK_SYMBOLS | 333 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN3_MPS128_X2_ACK_SYMBOLS | 224 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN3_MPS128_X4_ACK_SYMBOLS | 169 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN3_MPS256_X1_ACK_SYMBOLS | 512 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN3_MPS256_X2_ACK_SYMBOLS | 313 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN3_MPS256_X4_ACK_SYMBOLS | 214 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN4_BIT_HALF_PS | 125 | 0.5 ps | BASE4-CB §4.2 编码速率，推算 |
| GEN4_SYMBOL_PS | 500 | ps | BASE4-CB §3.6 符号时间单位 |
| GEN4_MPS128_X1_ACK_SYMBOLS | 333 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN4_MPS128_X2_ACK_SYMBOLS | 224 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN4_MPS128_X4_ACK_SYMBOLS | 169 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN4_MPS256_X1_ACK_SYMBOLS | 512 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN4_MPS256_X2_ACK_SYMBOLS | 313 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| GEN4_MPS256_X4_ACK_SYMBOLS | 214 | symbols | BASE4-CB pp.231–233 Tables 3-7–3-10 |
| REPLAY_SYMBOLS | 24576 | symbols | BASE4-CB p.217 允许范围内的参考选择 |
| REPLAY_EXTENDED_SYMBOLS | 81920 | symbols | BASE4-CB p.217 允许范围内的参考选择 |
| FC_UPDATE_PS | 30000000 | ps | BASE4-CB p.175 |
| FC_UPDATE_EXTENDED_PS | 120000000 | ps | BASE4-CB p.175 |
| FC_INIT_REPEAT_PS | 34000000 | ps | BASE4-CB pp.198–201 |
| SKP_SYMBOLS_INTERVAL | 1280 | symbols | BASE4-CB p.381 范围内的参考选择 |
| SKP_BLOCKS_INTERVAL | 370 | blocks | BASE4-CB p.382 范围内的参考选择 |
| LCRC_POLYNOMIAL | 79764919 | polynomial | BASE4-CB p.213 |
| DLLP_CRC_POLYNOMIAL | 4107 | polynomial | BASE4-CB p.209 |
| L1_IDLE_MIN_PS | 20000 | ps | BASE4-CB p.1093 |
| L1_EXIT_GUARD_PS | 40000 | ps | BASE4-CB p.366 |
| PHY_FEEDBACK_PS | 100000 | ps | 数字 PHY 默认反馈等待 100 ns，参考设计 |
| PHY_EQ_EVALUATION_PS | 1000000 | ps | 数字 PHY 每次均衡评估默认 1 µs，满足规定请求保持下界 |
| ASPM_IDLE_PS | 10000000 | ps | 启用 ASPM 后空闲 10 µs 的参考策略 |
| LT_DETECT_QUIET_PS | 12000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_DETECT_RETURN_TO_GEN1_MIN_PS | 1000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_POLLING_ACTIVE_PS | 24000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_POLLING_CONFIGURATION_PS | 48000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_CONFIGURATION_PS | 2000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_RECOVERY_LOCK_PS | 24000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_RECOVERY_CONFIG_PS | 48000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_EQUALIZATION_EP_PHASE01_PS | 12000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_EQUALIZATION_OTHER_PHASE_PS | 24000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_HOT_RESET_RECEIVER_HOLD_PS | 2000000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_POLLING_ACTIVE_MIN_TS1 | 1024 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_POLLING_RECEIVE_CONSECUTIVE | 8 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_POLLING_CONFIG_MIN_TS2_AFTER_FIRST_RX | 16 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_CONFIG_LANE_RECEIVE_TS1 | 2 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_CONFIG_COMPLETE_RECEIVE_TS2 | 8 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_CONFIG_COMPLETE_MIN_TS2_AFTER_FIRST_RX | 16 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_IDLE_RECEIVE_SYMBOLS | 8 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_IDLE_TRANSMIT_SYMBOLS_AFTER_FIRST_RX | 16 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_RECOVERY_RECEIVE_TS | 8 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_RECOVERY_SPEED_TS2_8B10B | 32 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_RECOVERY_SPEED_TS2_128B130B | 128 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_EQ_PHASE_RECEIVE_TS1 | 2 | count | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_EQ_REQUEST_HOLD_MIN_PS | 1000000 | ps | BASE4-CB pp.301–359,374–375 §4.2.6 |
| LT_SPEED_IDLE_SUCCESS_PS | 800000 | ps | BASE4-CB p.350 |
| LT_SPEED_IDLE_FAILURE_PS | 6000000 | ps | BASE4-CB p.350 |
| LT_SPEED_IDLE_MAX_EXTRA_PS | 1000000000 | ps | BASE4-CB p.350 |
| L1_REJECT_RETRY_PS | 10000000 | ps | BASE4-CB p.482 |
| RC_CRS_RETRY_PS | 100000000 | ps | 参考 RC 每次 CRS 重试等待 100 us |
| RC_CRS_RETRIES | 10000 | count | 参考 RC 最多 10000 次 CRS 重试，覆盖约 1 s |
| CPL_TIMEOUT_A_SHORT_PS | 100000000 | ps | BASE4-CB p.812，参考实现选择 100 us |
| CPL_TIMEOUT_A_LONG_PS | 10000000000 | ps | BASE4-CB p.812，参考实现选择 10 ms |
| CPL_TIMEOUT_B_SHORT_PS | 55000000000 | ps | BASE4-CB p.812，参考实现选择 55 ms |
| CPL_TIMEOUT_B_LONG_PS | 210000000000 | ps | BASE4-CB p.812，参考实现选择 210 ms |
| RC_LINK_WAIT_PS | 100000000000 | ps | 参考 RC 在数据链路不可用时最多保留一次上游请求 100 ms |

## 行为覆盖

实现和测试列定位相应源码与检查，验收结果见 verification.md。

### RATE 代际速率编码

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 PG213-STATUS。

实现落点为 gem5-ext/src/gw100/pcie/link/serializer.cc。

验证落点为 LinkSerializer.TwelveModes, ReferenceTest.test_serialization_twelve_modes。

### CREDIT_UNIT 六类信用与数据单位

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 ALTERA-FC, PG213-FC。

实现落点为 gem5-ext/src/gw100/pcie/link/flow_control.cc。

验证落点为 LinkCredits.InitializationAndExhaustion, PcieSpecTest.test_configuration_write_budget。

### RCB 完成拆分边界及头资源预留

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 PG213-RCB。

实现落点为 gem5-ext/src/gw100/pcie/link/link_transport.cc。

验证落点为 LinkTransport.WholeReadAndWireTag, LinkTransport.RejectIncompatibleCompletionRcb, tools/pcie_verify.py --matrix。

### RESOURCES 参考微架构资源预算

依据状态为 design，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 用户批准的参考设计。

实现落点为 gem5-ext/src/gw100/pcie/link/link_model.cc, link/replay.cc, link/link_transport.cc。

验证落点为 LinkReplay.ActualStoredBytes, LinkReplay.DescriptorCapacity, LinkTransport.RcPostedReservation, LinkModel.BackendRetry。

### BOUNDARIES 六个观测点与条件性能验收

依据状态为 design，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 用户批准的参考设计。

实现落点为 gem5-ext/src/gw100/pcie/gw100_pci_link.cc, link/link_model.cc。

验证落点为 LinkModel.SixPointsAndPipelines, LinkTransport.ReadWaitsForPostedVisibility, baremetal/pcie_link。

### TIMERS ACK/replay 定时规则

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 AM012-LL, PG054-LL, SIG-REPLAY, LATTICE-X4, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/link/link_model.cc。

验证落点为 LinkModel.AckDeadlineUnderDuplexTraffic, LinkModel.LostAckTimeoutAndDuplicate, LinkModel.ReplayRolloverRecovery。

### FRAMING 编码块、framing 与 SKP

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 SIG-SKP, PG213-STATUS, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/link/serializer.cc, link/packet.cc。

验证落点为 LinkSerializer.ContinuousPhase, LinkSerializer.SkipNeedsWholeEds, LinkPacket.StartTokenProtection。

### CRC LCRC、DLLP CRC 与 framing 检查

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 SIG-REPLAY, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/link/packet.cc。

验证落点为 LinkPacket.Crc32WireVector, LinkPacket.Crc16WireVector, LinkModel.DigitalFaultMatrix, tools/pcie_system_verify.py --suite faults。

### FLOW_CONTROL 信用初始化、累计更新与回绕

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 PG213-FC, ALTERA-FC, LATTICE-X4, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/link/flow_control.cc, link/link_model.cc。

验证落点为 LinkCredits.HeaderWrap, LinkCredits.DataWrap, LinkCredits.Scales, LinkModel.SequenceAndCreditWrap, LinkModel.ReceiveCreditBackpressure。

### WIRE_FIELDS TLP/DLLP 线上格式与完成匹配

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 PG213-CPL, PG213-STATUS, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/link/packet.cc, link/link_transport.cc。

验证落点为 LinkPacket.UnalignedBytes, LinkTransport.CompletionFaultMatrix, LinkTransport.ZeroLengthReadAndWriteNoSideEffects, LinkTransport.WireIdenticalTagReuseBoundary。

### ORDERING 事务排序与前进保证

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 PG213-STATUS, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/link/packet.cc, link/link_transport.cc。

验证落点为 LinkPacket.Ordering, LinkTransport.PostedVisibilityAndIntx, LinkTransport.ReadDoesNotWaitForYoungerPosted。

### LTSSM 数字训练与重训

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 PG213-STATUS, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/link/link_training.cc。

验证落点为 LinkModel.TwelveModesTrain, LinkModel.TrainingFailure, LinkModel.PhyCapabilityLimits, tools/pcie_system_verify.py --suite smoke。

### L1 ASPM L1 与电源转换

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 PG213-STATUS, AM012-LL, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/link/link_training.cc。

验证落点为 LinkModel.AspmEntryExit, LinkModel.PciPowerEntryExit, baremetal/dram_lifecycle, tools/pcie_system_verify.py --suite checkpoint。

### CONFIG_RESET 配置空间复位值与配置生效点

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 PG213-STATUS, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/config/config_space.cc, config/capability/pcie_capability.cc。

验证落点为 ConfigSpaceTest.CompletionTimeoutSelection, ConfigSpaceLink.SecondaryEndpointControlsReserved, ConfigSpaceLink.AerAndFlrPreservation, baremetal/pcie_config。

### ERROR_RESET AER、复位和错误生命周期

依据状态为 verified，核验范围为 Gen1, Gen2, Gen3, Gen4。

来源为 SIG-REPLAY, PG213-CPL, BASE4-CB。

实现落点为 gem5-ext/src/gw100/pcie/config/config_space.cc, gw100_pci_link.cc, hdma/hdma_engine.cc。

验证落点为 LinkTransport.ConfigurationCrsRetry, LinkTransport.ConfigurationCrsBound, LinkModel.HotResetAndLinkDown, baremetal/pcie_link_error, baremetal/dram_link_reset。
