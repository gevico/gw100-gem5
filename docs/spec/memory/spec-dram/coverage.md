# spec-dram 来源覆盖清单

<!-- 请勿手改；由 tools/dram_spec.py 生成 -->

档案版本：0.1.0；档案 SHA-256：3f2b4e5a5452c0a983ace3f2b555bf32ad473c74af82c58a58411293f56a0f07。

这是功能组级别的 G0 来源清单，尚未完成逐 MR 位域和逐命令约束展开。
下表的 planned: 是后续实现/测试落点，路径和用例名不表示已经存在或通过。
精确模型、固件初始化、独立时序及性能验收均未交付。

| 功能 | 分类 | SK1 PDF 页 | 数字行为/边界 | 实现落点 | 测试落点 |
|---|---|---|---|---|---|
| organization | required | 3, 5, 12, 16, 22, 29 | 封装资源、通道、rank、bank、行列和模式变化 | planned: gem5-ext/src/mem-tile/sk_hynix_device.* | planned: organization/address/mode |
| power_reset | required | 34, 35, 36, 37, 38, 39 | 上电/复位等待、双 VDD2 数字模式、受控掉电和数据有效性 | planned: sk_hynix_device | planned: init/reset/poweroff |
| analog_supply | analog_excluded | 34, 537, 538 | 模拟电压斜率、功耗电流和波形计算 | excluded | not_applicable |
| zq | required | 40, 41, 42, 43, 44, 46, 47, 48, 49, 50, 51, 52, 53, 54 | ZQ 数字命令、共享资源、停止/复位/锁存等待 | planned: sk_hynix_device + phy_feedback | planned: zq/sharing/waits/failures |
| cbt_fsp | required | 55, 56, 64, 69, 75, 83 | 三组 FSP、CBT 两模式、进入/退出和 CA VREF 数字编程 | planned: sk_hynix_device + firmware/fw/dram.c | planned: cbt/fsp/retention |
| leveling | required | 84, 85, 86, 87 | WCK2CK leveling 数字状态、命令和等待 | planned: sk_hynix_device + phy_feedback | planned: leveling/success/failure/timeout |
| dca_dcm | required | 88, 89, 90, 91, 92, 93, 94, 95, 96 | DCA/Read DCA/DCM 数字控制、能力和结果 | planned: sk_hynix_device + phy_feedback | planned: dca/dcm/capabilities |
| dq_fifo_training | required | 97, 102, 103, 104, 109, 110, 114, 116, 119, 123, 127 | Read DQ、FIFO 指针/图样、DMI 行为、RDQS 和 offset 训练 | planned: sk_hynix_device + phy_feedback + firmware/fw/dram.c | planned: training/pattern/pointer/timeout |
| analog_training | analog_excluded | 83, 89, 93, 127, 471 | 眼图、模拟电压/相位求解、模拟均衡；以可控反馈替身替代 | excluded: analog solver only | not_applicable |
| state_machine | required | 128, 129, 130, 131, 132 | 器件数字状态及合法转换 | planned: sk_hynix_device | planned: state/transitions/illegal |
| mode_registers | required | 136, 137, 138, 139, 140, 196 | LPDDR5X MR 逐项访问属性、复位掩码、FSP 复制、保留/未定义语义；须继续细化到位域 | planned: sk_hynix_device + generated definitions | planned: mr/all-fields/reset/access/fsp |
| private_mrs | vendor_unpublished | 150, 185, 186, 187 | MR9 测试私有语义及 MR47–54 的厂商 Serial ID；不编造返回值 | excluded: unknown vendor semantics | planned: monitor/undefined/vendor |
| command_bus | required | 197, 198, 199, 200 | CA 双边沿、双阶段命令、片选与总线占用 | planned: sk_hynix_device | planned: command/encoding/overlap |
| wck | required | 201, 202, 204, 207, 218, 423, 487, 488 | WCK 同步、启停、模式和 CKR 切换；增强模式受能力位控制 | planned: sk_hynix_device | planned: wck/edges/sync/rank |
| row_commands | required | 234, 246, 269, 273, 502 | ACT、PRE、READ、WRITE、Masked Write、分段 burst、自动预充电和数据可见性 | planned: sk_hynix_device | planned: row/data/masked/ap/timing-boundaries |
| refresh | required | 286, 292, 293, 294, 295 | REFab/REFpb、刷新地址、截止、推迟、温度倍率和自刷新前额外刷新 | planned: sk_hynix_device + controller | planned: refresh/deadline/temperature/traffic |
| low_power | required | 297, 299, 301, 303, 304, 305, 306, 307, 308, 336, 338, 339, 341, 346 | 自刷新、PASR/PARC、掉电、深睡眠及数据保持/失效 | planned: sk_hynix_device + firmware/fw/dram.c | planned: power/retention/d3/d0 |
| odt_digital | required | 359, 374, 380, 395, 398, 402, 404, 416 | ODT 与 NT-ODT 的数字模式、切换和 rank 总线约束；不求解阻抗 | planned: sk_hynix_device | planned: odt/mode/turnaround |
| clock_vref_thermal | required | 418, 419, 423, 424, 425, 426, 427, 428 | 时钟停止、频率、VRCG、VREF 数字等待、温度报告 | planned: sk_hynix_device + rational clock | planned: clock/fsp/thermal/boundaries |
| mpc_oscillator | required | 429, 430, 435, 436, 437 | 适用 MPC、WCK2DQI/O 振荡器启停和数字计数接口 | planned: sk_hynix_device + phy_feedback | planned: mpc/oscillator/waits |
| dvfs | required | 438, 439, 441, 443, 444, 445, 447, 448 | 合法 DVFSC/E-DVFSC/DVFSQ 数字切换；产品固件不实施运行中调频 | planned: sk_hynix_device | planned: dvfs/legal/illegal/retention |
| data_copy | required | 165, 448, 449, 450, 451, 452 | 条件适用：Read/Write Data Copy、DBI 组合；等待型号能力确认 | planned: sk_hynix_device | planned: copy/capability/data |
| write_x | required | 165, 453, 454, 455, 456 | 条件适用：Write-X、WXS 字节控制；等待型号能力确认 | planned: sk_hynix_device | planned: writex/capability/data |
| ppr | required | 171, 181, 182, 457, 458, 459 | PPR 保护键、地址修复、资源和保持规则 | planned: sk_hynix_device | planned: ppr/key/resource/reset |
| rfm | required | 141, 170, 188, 196, 460, 461, 462, 467, 468, 469, 470 | RFM/ARFM/DRFM/BRC 的能力、计数器和等待；等待厂商阈值 | planned: sk_hynix_device + controller | planned: rfm/counters/deadline/traffic |
| dfe_control | required | 167, 181, 191, 192, 193, 194, 195, 471, 472, 474, 475 | DFE 数字寄存器和模式约束，模拟反馈量不计算 | planned: sk_hynix_device + phy_feedback | planned: dfe/registers/capability |
| link_ecc_dbi | required | 147, 166, 182, 183, 476, 477, 478, 479, 480, 481, 482 | Link ECC 编解码、DBI 顺序、注错、计数、清除和综合征 | planned: sk_hynix_device | planned: ecc/dbi/syndrome/counters |
| single_ended | required | 482, 483, 484, 485, 486 | 单端时钟数字模式、切换序列和等待 | planned: sk_hynix_device | planned: single-ended/mode/timing |
| command_constraints | required | 489, 490, 491, 492, 493, 494, 495, 499, 502, 506, 517, 518, 522, 526 | BL/n、命令间隔、读写/ODT/AP/CAS/训练/MR/rank 约束 | planned: sk_hynix_device + protocol monitor | planned: every-constraint/before/at/after |
| core_timing | required | 531, 532, 533, 534, 535, 536 | LPDDR5X 各合法模式核心时序和温度 derating；尚未全量转录 | planned: generated definitions + sk_hynix_device | planned: timing/all-modes/boundaries |
| electrical | analog_excluded | 537, 578, 586, 591, 618 | 电气波形、眼图、SI 和电流计算；CK/WCK 数字频率上限仍须核对 | excluded: electrical solver only | not_applicable |

## G0 阻塞

来源或审查状态缺失时不能进入 G1。以下项目由档案自动推导：

- capabilities/mr0_features: NT-ODT 时序、延迟模式、Enhanced WCK、Optimized Refresh、DMI、Unified NT-ODT、Pre-Emphasis、Per-pin DFE 的型号只读位
- capabilities/mr1_features: CS ODT、ARFM、DRFM 的型号支持位
- capabilities/wck_osc_frequency_mode: WCK2DQ Oscillator Frequency Mode 支持位（OP[5]；OP[4] 是模式选择）
- capabilities/copy_writex_odtd: WDCFS、RDCFS、WXFS、ODTD-CSFS、WXS 的型号取值
- capabilities/dfe_read_dca: DFE、Read DCA 支持位；数字控制仍在范围内
- capabilities/rdqs_toggle_training: RD/WR-based WCK-RDQS_t Training 支持位
- capabilities/rfm_parameters: RFM required、RAAIMT、RAAMULT 为厂商烧录，未列型号数值；影响命令和性能
- capabilities/dvfsc_options: DVFSC/E-DVFSC 及 E-DVFSC ODT 的只读支持位
- capabilities/sbe_count_rule: SBEC_Rule 决定并发单比特错误的累计规则
- capabilities/rfm_extensions: RAADEC、RFMSB 的型号值
- capabilities/blast_radius: BRC support 的型号值
- conflict/eight_bank: 型号功能页写 8B 可在不高于 6400 Mbps 支持；LPDDR5X 地址转换和速度表只列 BG、16B
- review/field_coverage_complete: 未完成
- review/full_timing_table_complete: 未完成
- review/interface_complete: 未完成
- review/cross_review: 尚无通过记录

未知只读能力不作为“厂商私有行为”排除：编码和影响已公开，缺的是型号取值。
只有明确标为 vendor specific、且未公开语义的部分属于 vendor_unpublished。
