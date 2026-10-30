# GW100 ARM host（第一步：bare-metal）

默认走 **bare-metal**：直接加载一个裸机 ELF，不启动 Linux、不生成 DTB、
不使用 bootloader。

第一步验证：

1. ARM 裸机程序能在 `VExpress_GEM5_Foundation` 上执行；
2. PCIe 总线上已经挂了 `Gw100PciDevice`，裸机测试 `pcie_enum` 能枚举它并读写
   BAR0 寄存器。裸机测试的架构见 `baremetal/README.md`。

设备目前只有配置空间、BAR0（4 KiB）和几个最小寄存器：

| BAR0 偏移 | 内容 |
|---|---|
| `0x00` | VERSION，读回 `1` |
| `0x04` | STATUS，当前读回 `0` |
| `0x08` | CMD_ADDR（8 字节，可读写） |
| `0x10` | DOORBELL（4 字节，可读写） |

没有 DMA、没有中断、没有命令/完成记录，也还没有驱动。

## 构建 gem5

在仓库根目录执行 `make gem-build`。手工构建的命令和 `PYTHON_CONFIG` 的注意事项见
`gem5-ext/README.md` 的"构建"。

## 编译和运行

运行在仓库根目录的 Makefile 里进行，跑哪个 test、用哪个配置脚本都在那里选：

```sh
make run                              # 按需构建 gem5 和裸机测试，运行 hello
make run TEST=pcie_enum               # 改跑 baremetal/tests/pcie_enum.c
make run HOST=o3                      # 换成 O3 host 预设
make run GEM5_OPTS="--debug-flags=PciDevice"   # gem5 本身的选项
make test                             # 批量运行全部裸机测试并汇总
```

`GEM5_OPTS` 写在配置脚本之前，交给 gem5；`CONFIG_ARGS` 写在之后，交给配置脚本。

默认使用 `gem5/build/ARM/gem5.opt` 和 `gem5-ext/configs/run_gw100.py`。
每种组合有自己的输出目录 `gem5/m5out/<板卡>-<host>-<设备>`，例如
`gem5/m5out/arm_vexpress-timing-gw100`，扫描组合时互不覆盖。

`baremetal/Makefile` 只负责编译，也可以单独用，见 `baremetal/README.md`。

裸机程序经 semihosting 输出到 gem5 的 stdout，并用 semihosting 的 `SYS_EXIT`
结束仿真；退出子码就是 gem5 的进程退出码（0 通过、1 失败、2 异常）。

## 运行

```sh
cd gem5
./build/ARM/gem5.opt ../gem5-ext/configs/run_gw100.py \
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
| `boards/` | 平台、PCIe、中断、启动方式；用 `features()` 声明提供的能力 | `arm_vexpress`（VExpress_GEM5_Foundation，提供 `pcie`） | `BOARD` |
| `hosts/` | CPU 模型、核数、频率、内存、cache | `timing`（默认）、`o3`、`atomic` | `HOST` |
| `devices/` | 设备参数和 SimObject 的创建；用 `requires` 声明需要的能力 | `gw100` | `DEVICE` |

- 板卡不认识具体设备，只把入口脚本传进来的 PCI 设备挂到总线上；设备号由
  VExpress 平台从 1 起分配。
- 设备 `requires` 的能力板卡不提供时，仿真开始前就报错。例如以后设备需要
  MSI-X，而 Foundation 平台没有 ITS。
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
./build/ARM/gem5.opt ../gem5-ext/configs/run_gw100.py --workload linux
```

首次运行会下载 `arm-ubuntu-24.04-boot-with-systemd` 资源，需要网络。

## 下一步

- 把 BAR0 接到派发器寄存器；
- 加命令记录/完成记录和 DMA；
- 加 legacy INTx，再考虑 MSI-X；
- 把地址映射、BAR 和 ABI 常量从 `devices/gw100.py` 迁移到重建后的单一真源。
