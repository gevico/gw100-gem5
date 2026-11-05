# 多 ISA 可行性测试的 RV32 程序：每个核一份以 CORE_ID 编译的实例。
# 可行性测试，不是产品程序；make firmware-build 不构建它。用法见
# gem5-ext/configs/feasibility/README.md。
#
#   make -C firmware/feasibility/rv32_hello BUILD_DIR=<输出目录>

CROSS ?= riscv64-elf-
CC := $(CROSS)gcc
OBJDUMP := $(CROSS)objdump
BUILD_DIR ?= ../../../build/gw100-feasibility
# RV32 核的个数，与 multi_isa.py 的 --rv32-cores 一致
CORES ?= 16

ARCH := -march=rv32imac_zicsr -mabi=ilp32
CFLAGS := $(ARCH) -O2 -std=c11 -ffreestanding -fno-builtin -nostdlib \
	-mno-relax -Wall -Wextra -Werror
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -mno-relax -Wl,--gc-sections \
	-Wl,--no-warn-rwx-segments

SRCS := crt0.S main.c
IDS := $(shell seq 0 $$(($(CORES) - 1)))
ELFS := $(foreach k,$(IDS),$(BUILD_DIR)/rv32_hello_$(k).elf)

.PHONY: all clean
all: $(ELFS)

$(BUILD_DIR)/rv32_hello_%.elf: $(SRCS) link.ld
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DCORE_ID=$* $(LDFLAGS) -T link.ld $(SRCS) -o $@ -lgcc
	$(OBJDUMP) -d $@ > $(@:.elf=.dis)

clean:
	rm -rf $(BUILD_DIR)
