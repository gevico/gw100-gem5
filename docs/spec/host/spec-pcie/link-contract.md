# PCIe 数字链路契约

本文属于 spec-pcie v0.3。实施状态由共享覆盖矩阵与验收报告记录。

## 规范版本与核验

本轮采用 PCI Express Base Specification 4.0 Version 1.0-c 修订标记稿。
封面日期为 2017-09-27，p.6 记录 2017-09-29 与最终 NCB 版本对齐的修改。
原件来自用户允许核验的公开镜像，固定 Git 提交、文件字节数和 SHA-256
均登记为 BASE4-CB。核验使用渲染原页，蓝色删除内容排除，红色新增内容纳入。
官方书目发布日期为 2017-10-05。镜像摘要固定本次读取内容，官方公开摘要
尚未取得。2019-09-02 勘误的适用性另列核验项。

2017-08-28 候选件含评审说明，候选比较记录保存在 `source_audit`。
全部协议规则与参考选择集中在 `spec-src/pcie/gw100-link.json`。

## 层次与资源

链路内核拥有双向串行传输、DLLP、序号、信用和 replay。端口适配器拥有
Host 请求、EP 服务、缓存行拆分、完成重组和有限后端队列。EP 的请求账本
拥有 tag、完成预留、隔离和完成超时，线上完成以 Requester ID 与 tag 匹配。

- 每侧 replay 数据为 4 KiB，按保留的序号、TLP 和 LCRC 字节数以 16 B
  对齐分配。元数据另设 128 项，每项 16 B。
- 每侧接收 PH 32 项、PD 2 KiB、NPH 16 项、NPD 256 B。
- EP 和本轮 RC 在线上宣告无限 Completion 信用。发读前预留实际完成资源，
  EP 数据 32 KiB、头 576 项。RC 使用相同预算。无限广告依赖请求者的有限预留。
- 待发 P、NP、Completion 各自受共享档案的头部和数据预算约束。
  DLLP 最多 16 项，累计 ACK 和同类累计 FC 更新合并为最新值。
- 后端最多 64 个操作，返回反压继续占用所属请求和接收存储。
- RC 为 posted 保留至少 16 个后端槽，NP 同时最多占 48 个。EP 维持
  64 个共享槽。这是本轮参考 RC 策略，新增成本为分类准入计数。
  纯双向读的 RC 目标槽上界相应使用 48，独立参考按分类资源分别计算。
- 每端最多 16 个 RX NP 槽各自固定首次到达时已有的 posted 依赖，
  最坏对应 16×64 位依赖矩阵及索引元数据。后续写可继续越过阻塞的 NP。

## 时间与调度

RC、EP 均为 250 MHz，每侧适配流水为两周期，单向传播为 5 ns。
这些为参考微架构选择。串行传输使用精确整数相位，事件量化为 1 ps。

- Gen1/2 使用 8b/10b。Gen3/4 使用连续 128b/130b 数据块，包之间保留块相位。
- SKP 调度选 1280 个符号时间或 370 个编码块。发送推迟至包边界。
  Gen3/4 的 EDS、块尾填充、SKP 和新数据流 SDS 分别计量。
- P、NP、Completion 的可发送性先满足 BASE4-CB Table 2-39，再公平轮询。
  被阻塞的 NP 允许规定的 P 和 Completion 越过。相同事务的完成保持地址递增。
- ACK 在累计八个 TLP 或规范最大延迟到期时安排，优先等待当前包结束后发送。
  具体期限按速率、宽度、MPS 查表。
- 支持 Gen4 的端口在所有速率下使用简化 replay 期限。本轮选择普通 24576、
  Extended Synch 81920 个符号时间。Recovery 和 Configuration 暂停计数。
- FC 释放经两周期形成累计更新，有限信用周期更新为 30 µs，Extended Synch
  为 120 µs。InitFC 与 Data Link Feature 的重复发送上界为 34 µs。
- Gen4 端口支持 Scaled FC，本轮预算选择 scale 1。完成广告与实体完成存储分别核算。

## 接受与完成

ACCEPT、DLL_COMMIT、RX_COMPLETE、ACK_RETURN、TARGET_VISIBLE、
REQUEST_COMPLETE 按 spec-pcie §1.1.1 分开观测。
CPU posted 写在 RC 本地接受后获得上游响应，载荷副本由链路保留。
配置读写、MMIO 读经线上 Completion 返回。HDMA 完成超时从 DLL_COMMIT 起算。

RC 接收完整 DMA TLP 后将 Host 内存访问拆成缓存行 timing 请求。
Byte Enable 保留到目标，完成拆包满足 RCB、有效 MPS 和独立 Completion 上限。
同 TC 的写入、完成记录和 MSI-X 遵循共同发送路径的顺序。
NP 目标执行等待首次到达时已有的 posted 写可见。阻塞期间新到达的
posted 写保持独立进展，其服务等待并入各自目标时间。

启动窗口的 posted 写持有 EP 后端槽，直到全部 ML `BOOT_WR` 返回确认。
启动写完成表最多 64 项，每个最大 TLP 最多形成五个 ML 子写。设备复位
结算这些本地完成回调，旧确认无法再次更新新代次的提交计数。

### 原生系统接入

共享 `LinkTransport` 内嵌于 `Gw100PciDevice`，连接两个逻辑方向的 RC、EP
服务。Host 的原生 PCI 总线继续拥有 ECAM、BDF 和 BAR 范围通知。
原生 PIO 的延后响应接口承载配置与 BAR completion。HDMA 把完整 TLP
交给链路，RC 接收后才调用缓存行 Host 内存后端。INTx 和错误 Message
使用同一发送路径。MSI-X 的目标执行等待此前 posted 数据写可见。

RC 原生入口最多保留 64 个包，包含上游响应反压占用。每端后端保留
64 个完整操作。Host 侧最大 512 B 读在 64 B 缓存行下最多形成九个子包，
对应最多 576 个后端子包描述符。这些预算是参考微架构的资源成本。

原生 Host 入口等待计入 Host 端到端观测。链路 `ACCEPT` 标记完整 TLP
进入本地事务队列，目标服务由 `TARGET_BEGIN`、`TARGET_END` 单列。
`REQUEST_RETIRED` 表示超时或隔离后停止上游通知。

RC 的有效 MPS 跟随 EP 的软件配置，作为本轮参考 RC 策略。配置访问的
UR/CA 在 RC 返回全一读值。数据链路不可用时，RC 对一个原生请求的本地
等待上界取 100 ms。DLL 提交后的完成超时另行计时。CRS 的重试间隔取
100 µs，上限为 10000 次。

Device Control 2 支持 A/B 范围选择。默认值使用配置的 50 ms，编码
1、2、5、6 分别选择 100 µs、10 ms、55 ms、210 ms。选择在 DLL 提交时
采样，已有请求保留其期限。来源为 BASE4-CB p.812，具体值采用各范围
上界。未定义编码保持此前选择。

## 生命周期

冷启动经 Gen1 Polling、Configuration 和 L0，较高代际经 Recovery 升速。
Polling 的 1024 TS1、TS2 握手下界、各阶段超时及均衡请求保持时间按档案执行。
数字 PHY 提供检测、锁定、均衡成功或失败反馈，反馈在相应状态接受。

FLR 按 BASE4-CB pp.587–588 保留 MPS、ASPM、RCB、Common Clock、
Extended Synch、链路均衡、VC0、Data Link Feature、PHY16 和 sticky 状态。
功能使能清零，链路序号、信用和 replay 持续运行。INTx 撤销须先进入有序消息路径。
提交前取消与提交后事务分别结算。隔离 tag 继续处理迟到完成，线上不可区分的
同字段 tag 复用风险按 spec-pcie §10.3 记录。

ASPM L1 默认关闭。使能后通过 PM DLLP 和必要的拒绝消息协商，双方 replay
清空后发送 EIOS 进入 L1。L1 的 FC 计时冻结，Completion Timeout 持续计时。
退出经 Recovery。D3hot 采用 PCI-PM 的 L1 握手并由功能电源状态门控主动业务。

PERST、Hot Reset、实际 DL_Down 清理链路层，随后重新训练和初始化信用。
设备完全复位沿用功能复位边界。drain 覆盖待发、replay、后端和返回反压。
排空后的 L0/L1 状态可保存并恢复。

### 恢复边界

`pcie_checkpoint_fixture.py` 使用正常 Host timing 端口、原生 EP 寄存器
和相同的链路实现。三个独立 gem5 进程验证 L0 保存、L0 恢复后保存 L1、
L1 恢复后继续访问。保存内容覆盖 BAR、配置、MSI-X、HDI、HDMA、tag LRU、
门控、信用、序号、串行相位和计数。

该夹具的设备后端为 4 KiB，管理 Tile 的全系统状态保存列入后续关口。
八通道 32 GiB 内存的排空、自刷新与数据保存由内存组件验证覆盖。
正常产品配置的 ROM、固件初始化、COPY 和中断闭环由软件矩阵验收。

超时 tag 的数据预留及时释放，匹配元数据保留到隔离结束。完整迟到完成
在隔离期内被消费，应用回调保持结束状态。隔离期后重新使用相同
Requester ID、tag、TC、地址、长度时，线上字段完全相同的旧完成可匹配到
新事务。`WireIdenticalTagReuseBoundary` 固定这一可观察结果。

## 参考假设与模型边界

- 控制器时钟、两周期适配流水、5 ns 单向传播和所有缓冲预算由共享档案
  拥有。RC 与 EP 的完整 TLP 后端各有 64 个槽。
- 数字 PHY 默认反馈为 100 ns，均衡附加等待为 1 µs。ASPM 空闲触发阈值
  为 10 µs。失败在支持的 LTSSM 检查点注入，lane 能力与最高代际可配置。
- RC 的错误通过 trace 和计数观测。EP 的 AER 配置空间实现状态、屏蔽、
  严重程度、头日志和错误 Message。商业 Root Port 的配置空间另列后续接入。
- ECRC、10 位 tag、SRIS、L1 子状态和多 VC 的能力取关闭值。本轮采用单
  PF、VC0。普通数据写的 poisoned 策略选择丢弃并报告，来源为
  BASE4-CB pp.185–186。
- 实物校准与冻结基线后的勘误适用性继续单列。性能比较对象为独立程序
  和逐边沿手算。临时设备互连的替换关口见 `spec-clock-reset-power`。

## 验证记录

G0 工具回归 25/25 通过，原件字节数和摘要复算一致，四端生成物检查通过。
后续运行结果、源码指纹和各场景状态由验收报告逐项登记。
G0 修改后的既有功能基线通过默认矩阵 39/39、O3 22/22 和组件测试 103/103。
这些结果为链路接入前的回归基线。
