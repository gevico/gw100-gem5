# GW100 ARM host（第一步：bare-metal）

默认走 **bare-metal**：直接加载一个裸机 ELF，不启动 Linux、不生成 DTB、
不使用 bootloader。

第一步验证：

1. ARM 裸机程序能在 `VExpress_GEM5_Foundation` 上执行；
2. PCIe 总线上已经挂了 `Gw100PciDevice`（裸机程序暂时只写 UART 并退出，
   还没有去枚举/访问设备）。

设备目前只有配置空间、BAR0（4 KiB）和几个最小寄存器：

| BAR0 偏移 | 内容 |
|---|---|
| `0x00` | VERSION，读回 `1` |
| `0x04` | STATUS，当前读回 `0` |
| `0x08` | CMD_ADDR（8 字节，可读写） |
| `0x10` | DOORBELL（4 字节，可读写） |

没有 DMA、没有中断、没有命令/完成记录，也还没有驱动。

## 构建 gem5

```sh
cd gem5
../.venv/bin/scons build/ARM/gem5.opt EXTRAS="$PWD/../gem5-ext" -j8
```

macOS 上需要按 `gem5-ext/README.md` 的说明补
`PYTHON_CONFIG=/opt/homebrew/opt/python@3.12/bin/python3.12-config`。

## 编译和运行

推荐用仓库根目录的 `baremetal/Makefile`：

```sh
make -C baremetal          # 构建 build/gw100-baremetal/hello.elf
make -C baremetal check    # 查看 ELF 架构和入口地址
make -C baremetal run      # 构建并启动 gem5 裸机仿真
make -C baremetal disasm   # 反汇编
make -C baremetal clean    # 删除构建产物
```

`make run` 默认使用 `gem5/build/ARM/gem5.opt` 和
`gem5-ext/configs/run_gw100.py`，输出目录是 `gem5/m5out`。

如果手工编译：

```sh
cd gem5
mkdir -p ../build/gw100-baremetal
aarch64-linux-gnu-gcc -nostdlib -nostartfiles -ffreestanding \
    -Wl,-T,../baremetal/linker.ld \
    -o ../build/gw100-baremetal/hello.elf \
    ../baremetal/hello.S
```

程序向 Foundation 的 UART（`0x1C090000`）写 `G`，然后用 semihosting
`SYS_EXIT` 结束仿真。

## 运行

```sh
cd gem5
./build/ARM/gem5.opt ../gem5-ext/configs/run_gw100.py \
    --workload baremetal \
    --kernel ../build/gw100-baremetal/hello.elf
```

预期：

- 串口/标准输出出现 `G`；
- 仿真通过 semihosting 退出；
- `m5out/config.ini` 里能看到 `Gw100PciDevice` 挂在 PCIe 总线上。

当前使用 O3 CPU，启动会明显变慢；如果只想快速验证，可以把
`board_arm.py` 里的 `CPUTypes.O3` 临时改回 `CPUTypes.TIMING`。

## Linux（可选，后续对比用）

```sh
./build/ARM/gem5.opt ../gem5-ext/configs/run_gw100.py --workload linux
```

首次运行会下载 `arm-ubuntu-24.04-boot-with-systemd` 资源，需要网络。

## 下一步

- 在裸机程序里扫描 PCIe 配置空间，找到 GW100 并读 BAR0 的 VERSION；
- 把 BAR0 接到派发器寄存器；
- 加命令记录/完成记录和 DMA；
- 加 legacy INTx，再考虑 MSI-X；
- 把地址映射、BAR 和 ABI 常量从 `params.py` 迁移到重建后的单一真源。
