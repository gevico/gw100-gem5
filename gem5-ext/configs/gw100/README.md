# GW100 ARM host（第一步：bare-metal）

默认走 **bare-metal**：直接加载一个裸机 ELF，不启动 Linux、不生成 DTB、
不使用 bootloader。

第一步验证：

1. ARM 裸机程序能在 `VExpress_GEM5_V2` 上执行；
2. PCIe 总线上挂着 `Gw100PciDevice`，它的寄存器层按 `docs/spec/host/spec-pcie.md`
   实现，由裸机测试 `pcie_*` 验证。裸机测试的架构见 `baremetal/README.md`。

当前正常配置为 `gw100`，Host 支持 timing/O3，默认 Gen4 x4。
`gw100_compat` 承载原有 atomic 功能回归。能力矩阵由
`baremetal/test-matrix.json` 声明。

| 部分 | 正常配置 |
|---|---|
| 链路 | Gen1–Gen4、x1/x2/x4，TLP/DLLP、CRC、信用、replay、数字训练与 L1 |
| 配置空间 | PM、PCI Express、MSI-X、AER、Secondary PCIe、Data Link Feature、PHY16 |
| BAR0 | 全局、窗口、启动、HDMA、HDI 和中断寄存器 |
| BAR2 | 固件装载与调试窗口，正常访问经异步 timing 路径 |
| 设备内存 | 四 Memory Tile、八通道、双 rank、32 GiB 通用 LPDDR5-8433 |
| 软件 | 真实 RISC-V ROM、固件初始化、HDI COPY、MSI-X/INTx 与 ITS |
| 生命周期 | FLR、设备完全复位、PERST、Hot Reset、Link Down、D3hot/D0 |

PCIe、DRAM、临时互连与管理 Tile 的参考假设分别记录在
`gem5-ext/README.md` 理想化清单和 PCIe 验收报告中。

## 构建 gem5

在仓库根目录执行 `make gem-build`。手工构建的命令和 `PYTHON_CONFIG` 的注意事项见
`gem5-ext/README.md` 的"构建"。

## 编译和运行

运行在仓库根目录的 Makefile 里进行，跑哪个 test、用哪个配置脚本都在那里选：

```sh
make run                              # 按需构建 gem5 和裸机测试，运行 hello
make run TEST=pcie_regs               # 改跑 baremetal/tests/pcie_regs.c
make run HOST=o3                      # 换成 O3 host 预设
make run GEM5_OPTS="--debug-flags=Gw100Pcie"   # gem5 本身的选项，例如设备的调试输出
make test                             # 批量运行全部裸机测试并汇总
```

`GEM5_OPTS` 写在配置脚本之前，交给 gem5；`CONFIG_ARGS` 写在之后，交给配置脚本。

默认使用 `gem5/build/GW100/gem5.opt` 和 `gem5-ext/configs/run_gw100.py`。
每种组合有自己的输出目录 `gem5/m5out/<板卡>-<host>-<设备>`，例如
`gem5/m5out/arm_vexpress-timing-gw100`，扫描组合时互不覆盖。

`baremetal/Makefile` 只负责编译，也可以单独用，见 `baremetal/README.md`。

裸机程序经 semihosting 输出到 gem5 的 stdout，并用 semihosting 的 `SYS_EXIT`
结束仿真；退出子码就是 gem5 的进程退出码（0 通过、1 失败、2 异常）。

## 运行

```sh
cd gem5
./build/GW100/gem5.opt ../gem5-ext/configs/run_gw100.py \
    --workload baremetal \
    --kernel ../build/gw100-baremetal/hello.elf \
    --max-ticks 10000000000
```

预期：

- 标准输出出现 `[INFO] hello from GW100 baremetal` 和 `[PASS]`；
- 最后一行是 `bare-metal exit: semi:ADP_Stopped_ApplicationExit (code 0)`，gem5 的进程退出码为 0；
- 输出目录的 `config.ini` 里能看到 `Gw100PciDevice` 挂在 PCIe 总线上。

`--max-ticks` 是仿真 tick 上限，到达时退出码为 124，用来防止程序死循环；省略表示不限。

## 板卡与 host 预设

板卡、host 和设备分三层，互相独立，只在入口脚本 `run_gw100.py` 里组合：

```
gem5-ext/configs/
├── run_gw100.py        # 唯一的组合点：--board / --host / --device
└── gw100/
    ├── boards/         # 平台结构，只提供插槽
    ├── hosts/          # host 预设
    ├── devices/        # 被测设备预设
    └── workload.py     # 裸机 / Linux 的启动方式
```

| 目录 | 决定什么 | 现有 | Makefile 变量 |
|---|---|---|---|
| `boards/` | 平台、PCIe、中断、启动方式；用 `features()` 声明提供的能力 | `arm_vexpress`（VExpress_GEM5_V2，GICv3 带 ITS，提供 `pcie`） | `BOARD` |
| `hosts/` | CPU 模型、核数、频率、内存、cache | `timing`（默认）、`o3`、`atomic` | `HOST` |
| `devices/` | 设备参数和 SimObject 的创建；用 `requires` 声明需要的能力 | `gw100` | `DEVICE` |

- 板卡不认识具体设备，只把入口脚本传进来的 PCI 设备挂到总线上；设备号由
  VExpress 平台从 1 起分配。
- 设备 `requires` 的能力板卡不提供时，仿真开始前就报错。例如以后接入
  一块没有 ITS 的平台，而设备需要把 MSI-X 交给 CPU。
- 加新的 host 或设备变体，只在 `hosts/` 或 `devices/` 里登记一项。

`o3` 比 `timing` 慢一个数量级，只在测 host 侧软件开销或检查驱动内存序时用。
`atomic` 是功能模式：访问立即完成，只验证功能正确，不产出性能数字。
这几个 host 预设都还没有对标真实的端侧主机。

扫描组合：

```sh
for h in timing o3; do make run HOST=$h; done
```

## Linux（可选，后续对比用）

```sh
./build/GW100/gem5.opt ../gem5-ext/configs/run_gw100.py --workload linux
```

首次运行会下载 `arm-ubuntu-24.04-boot-with-systemd` 资源，需要网络。

## 下一步

- 中断：INTx 投递，MSI-X 表与消息发送；
- Host DMA 引擎与命令协议（HDI）；
- 把地址映射、BAR 和 ABI 常量从 `devices/gw100.py` 迁移到重建后的单一真源。
