# 管理 Tile 的 RISC-V 程序：启动 ROM（rom/）与固件（fw/，后续阶段）。
# 只负责编译；由顶层 Makefile 的 firmware-build 调用。
#
#   make -C firmware BUILD_DIR=<输出目录>

CROSS ?= riscv64-elf-
CC := $(CROSS)gcc
OBJDUMP := $(CROSS)objdump
OBJCOPY := $(CROSS)objcopy
PYTHON ?= python3
# 固件版本号，写入镜像头的 FW_VERSION
FW_VERSION ?= 0x00010000
BUILD_DIR ?= ../build/gw100-firmware

# 管理 CPU：RV64IMAC + Zicsr + Zifencei，M/U 模式（spec-mgmt-tile §3.2）
ARCH := -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany
CFLAGS := $(ARCH) -Os -std=c11 -ffreestanding -fno-builtin -nostdlib \
	-mno-relax -Wall -Wextra -Werror -Icommon
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -mno-relax -Wl,--gc-sections

ROM_SRCS := rom/crt0.S rom/rom.c common/sha256.c common/string.c \
	common/gw100_flr.c
FW_SRCS := fw/crt0.S fw/fw.c common/string.c common/gw100_flr.c \
	common/gw100_hdma.c

.PHONY: all clean
all: $(BUILD_DIR)/rom.elf $(BUILD_DIR)/fw.img $(BUILD_DIR)/fw_wdt.img \
	$(BUILD_DIR)/fw_hdma.img

$(BUILD_DIR)/rom.elf: $(ROM_SRCS) rom/rom.ld $(wildcard common/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -T rom/rom.ld $(ROM_SRCS) -o $@
	$(OBJDUMP) -d $@ > $(@:.elf=.dis)

# 测试用固件：开启 BAR2 调试窗口（见 fw/fw.c）
$(BUILD_DIR)/fw.elf: $(FW_SRCS) fw/fw.ld $(wildcard common/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DGW_FW_DEBUG_WIN $(LDFLAGS) -Wl,--no-warn-rwx-segments \
		-T fw/fw.ld $(FW_SRCS) -o $@
	$(OBJDUMP) -d $@ > $(@:.elf=.dis)

# 看门狗的测试固件：置位 FW_READY 后不再喂狗（见 fw/fw.c）
$(BUILD_DIR)/fw_wdt.elf: $(FW_SRCS) fw/fw.ld $(wildcard common/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DGW_FW_WDT_TEST $(LDFLAGS) -Wl,--no-warn-rwx-segments \
		-T fw/fw.ld $(FW_SRCS) -o $@
	$(OBJDUMP) -d $@ > $(@:.elf=.dis)

# HDMA 的测试固件：按约定的 host 地址做传输（见 fw/hdma_test.c）
$(BUILD_DIR)/fw_hdma.elf: $(FW_SRCS) fw/hdma_test.c fw/fw.ld \
		$(wildcard common/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DGW_FW_HDMA_TEST $(LDFLAGS) \
		-Wl,--no-warn-rwx-segments -T fw/fw.ld $(FW_SRCS) fw/hdma_test.c \
		-o $@
	$(OBJDUMP) -d $@ > $(@:.elf=.dis)

$(BUILD_DIR)/%.img: $(BUILD_DIR)/%.elf ../tools/fwpack.py
	$(OBJCOPY) -O binary $< $(@:.img=.bin)
	$(PYTHON) ../tools/fwpack.py --elf $< --bin $(@:.img=.bin) --out $@ \
		--version $(FW_VERSION)

clean:
	rm -rf $(BUILD_DIR)
