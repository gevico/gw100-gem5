# NoC G0 覆盖矩阵

档案摘要为 `84bb0b9c82f183d001381c17d85c5ef924dd52365c3b2cd41c8caa090be66b77`。

| 项 | 定义 | 状态 | 检查 |
|---|---|---|---|
| TOPOLOGY | 坐标、节点、窗口、来源与 DPA | defined | tools/noc_spec_test.py::test_topology_and_sources |
| PACKET | 报头、字节使能、填充与 opcode | defined | tools/noc_spec_test.py::test_header_roundtrip |
| ROUTING | 全部 XY 路由及 VN 通道依赖 | defined | tools/noc_spec_test.py::test_all_xy_routes |
| MULTICAST | 分支复制、目的位图和部分结果 | defined | tools/noc_spec_test.py::test_multicast_partition |
| CREDIT | 完整包信用、接收与返回反压 | defined | tools/noc_spec_test.py::test_credit_reservation |
| RESOURCES | 队列、事务、CDC、镜像及控制线成本 | defined | tools/noc_spec_test.py::test_resource_costs |
| TIMING | 时钟相位、无竞争逐段预算 | defined | tools/noc_spec_test.py::test_timing_fixed_vectors |
| PERFORMANCE | 分层计时与条件性性能矩阵 | defined | tools/noc_spec_test.py::test_layered_contract |
| PROTECTION | 父请求预检与快照同步 | defined | tools/noc_spec_test.py::test_protection_snapshot |
| ORDERING | 写可见性、同地址顺序及 fence | defined | tools/noc_spec_test.py::test_completion_and_ordering |
| LIFECYCLE | CRC、隔离、FLR 与全复位 | defined | tools/noc_spec_test.py::test_lifecycle_contract |
| DEPENDENCIES | 当前 DRAM/PCIe 参数与外部服务依赖 | defined | tools/noc_spec_test.py::test_dependency_consistency |

## 实施关口

| 关口 | 状态 | 证据 |
|---|---|---|
| G0 | complete | docs/spec/interconnect/spec-noc/verification.md、tools/noc_spec_test.py |
| G1 | pending |  |
| G2 | pending |  |
| G3 | pending |  |

## 报头布局

位 0 先占据小端字节的最低位。

| 字段 | LSB | 位数 |
|---|---|---|
| opcode | 0 | 4 |
| vn | 4 | 2 |
| flags | 6 | 2 |
| initiator | 8 | 5 |
| destination | 13 | 5 |
| sender | 18 | 5 |
| admitted | 23 | 1 |
| transaction | 24 | 16 |
| address | 40 | 40 |
| length | 80 | 8 |
| destinations | 88 | 24 |
| status | 112 | 8 |
| reserved | 120 | 8 |

## 共享参数

| 参数 | 值 | 单位 | 依据 |
|---|---|---|---|
| MESH_COLS | 6 | nodes | GW100 G0 参考设计选择 |
| MESH_ROWS | 4 | nodes | GW100 G0 参考设计选择 |
| CORE_PERIOD_PS | 1000 | ps | GW100 G0 参考设计选择 |
| TICK_PS | 1 | ps | GW100 G0 参考设计选择 |
| FLIT_BYTES | 64 | B | GW100 G0 参考设计选择 |
| HEADER_BYTES | 16 | B | GW100 G0 参考设计选择 |
| BYTE_ENABLE_BYTES | 8 | B | GW100 G0 参考设计选择 |
| MAX_DATA_BYTES | 64 | B | GW100 G0 参考设计选择 |
| VN_COUNT | 4 | classes | GW100 G0 参考设计选择 |
| VCS_PER_VN | 1 | VC | GW100 G0 参考设计选择 |
| INPUT_DEPTH_FLITS | 4 | flits | GW100 G0 参考设计选择 |
| OUTPUT_DEPTH_FLITS | 1 | flits | GW100 G0 参考设计选择 |
| ROUTER_PIPELINE_CYCLES | 2 | cycles | GW100 G0 参考设计选择 |
| NI_PIPELINE_CYCLES | 2 | cycles | GW100 G0 参考设计选择 |
| LINK_CYCLES_PER_FLIT | 1 | cycles | GW100 G0 参考设计选择 |
| CREDIT_RETURN_CYCLES | 1 | cycles | GW100 G0 参考设计选择 |
| PACKET_SLOT_BYTES | 128 | B | GW100 G0 参考设计选择 |
| ROUTER_DESCRIPTOR_BYTES | 16 | B | GW100 G0 参考设计选择 |
| CDC_DESCRIPTOR_BYTES | 16 | B | GW100 G0 参考设计选择 |
| CDC_PACKET_BYTES | 128 | B | GW100 G0 参考设计选择 |
| CDC_DEPTH | 8 | packets | GW100 G0 参考设计选择 |
| CDC_SYNC_STAGES | 2 | cycles | GW100 G0 参考设计选择 |
| NI_RX_PACKETS_PER_VN | 8 | packets | GW100 G0 参考设计选择 |
| DATA_SLOTS_MGMT | 16 | fragments | GW100 G0 参考设计选择 |
| DATA_SLOTS_HDMA | 64 | fragments | GW100 G0 参考设计选择 |
| DATA_SLOTS_BAR2 | 16 | fragments | GW100 G0 参考设计选择 |
| DATA_SLOTS_COMPUTE | 16 | fragments | GW100 G0 参考设计选择 |
| COMMAND_SLOTS_MGMT | 8 | transactions | GW100 G0 参考设计选择 |
| CONTROL_SLOTS_PER_NI | 4 | transactions | GW100 G0 参考设计选择 |
| TRANSACTION_REQUEST_BYTES | 128 | B | GW100 G0 参考设计选择 |
| TRANSACTION_RESPONSE_BYTES | 128 | B | GW100 G0 参考设计选择 |
| TRANSACTION_META_BYTES | 32 | B | GW100 G0 参考设计选择 |
| PARENT_DESCRIPTOR_BYTES | 64 | B | GW100 G0 参考设计选择 |
| PROTECTION_SNAPSHOT_BYTES | 64 | B | GW100 G0 参考设计选择 |
| REGION_RECORD_BITS | 81 | bits | GW100 G0 参考设计选择 |
| SIDEBAND_FLIT_BITS | 5 | bits | GW100 G0 参考设计选择 |
| SIDEBAND_CREDIT_BITS | 3 | bits | GW100 G0 参考设计选择 |
| CREDIT_STATE_BITS_PER_VC | 9 | bits | GW100 G0 参考设计选择 |
| EGRESS_RR_BITS | 5 | bits | GW100 G0 参考设计选择 |
| INPUT_RR_BITS | 3 | bits | GW100 G0 参考设计选择 |
| ARRIVAL_COUNTER_BITS | 64 | bits | GW100 G0 参考设计选择 |
| END_TO_END_GOAL_PS | 1000000 | ps | GW100 G0 参考设计选择 |
| REFERENCE_TARGET_PS | 250000 | ps | GW100 G0 参考设计选择 |
| WARMUP_PS | 20000000 | ps | GW100 G0 参考设计选择 |
| MEASURE_PS | 100000000 | ps | GW100 G0 参考设计选择 |
| SEED | 1 | integer | GW100 G0 参考设计选择 |
| SOURCE_STATE_BYTES | 32 | B | GW100 G0 来源 tag、顺序号与状态预算 |
| NI_CONTROL_BYTES | 256 | B | GW100 G0 每 NI 控制、错误快照及 16 个 64 位计数器预算 |
| EXEC_REGIONS_PER_COMPUTE | 8 | regions | spec-address-map §5.2 ct.exec_regions |
| MGMT_REGIONS_PER_COMPUTE | 8 | regions | GW100 G0 管理 SRAM 开放区间的参考容量 |
| PERMISSION_REGION_BITS | 81 | bits | 两个 40 位端点与一个使能位，推算 |
| NI_CREDIT_STATE_BITS_PER_VC | 15 | bits | 八个 128 B 包槽共 16 个存储 flit 信用，三个五位计数器，推算 |
