# GW100 ARM 裸机示例的构建与运行规则。
#
# 在仓库根目录执行：
#   make -C baremetal            # 构建 build/gw100-baremetal/hello.elf
#   make -C baremetal run        # 构建并启动 gem5
#   make -C baremetal clean

CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
OBJDUMP ?= $(CROSS_COMPILE)objdump
READELF ?= $(CROSS_COMPILE)readelf

SRC ?= hello.S
LDSCRIPT ?= linker.ld
BUILD_DIR ?= ../build/gw100-baremetal
ELF := $(BUILD_DIR)/hello.elf

CFLAGS ?= -nostdlib -nostartfiles -ffreestanding -g
LDFLAGS ?= -Wl,-T,$(LDSCRIPT)

GEM5 ?= build/ARM/gem5.opt
RUN_CONFIG ?= ../gem5-ext/configs/run_gw100.py

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.PHONY: all clean run disasm check help

all: $(ELF)

$(ELF): $(SRC) $(LDSCRIPT) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

$(BUILD_DIR):
	mkdir -p $@

run: $(ELF)
	cd ../gem5 && $(GEM5) $(RUN_CONFIG) \
		--workload baremetal --kernel $(abspath $(ELF))

disasm: $(ELF)
	$(OBJDUMP) -d $<

check: $(ELF)
	file $<
	$(READELF) -h $< | grep -E 'Machine|Entry'

clean:
	@test -n "$(BUILD_DIR)" || { echo "BUILD_DIR 不能为空"; exit 1; }
	rm -rf -- $(BUILD_DIR)

help:
	@echo "all      构建 $(ELF)"
	@echo "run      构建并启动 gem5 裸机仿真"
	@echo "disasm   反汇编 $(ELF)"
	@echo "check    查看 ELF 架构和入口地址"
	@echo "clean    删除 $(BUILD_DIR)"
	@echo ""
	@echo "变量：CROSS_COMPILE=$(CROSS_COMPILE)"
	@echo "      GEM5=$(GEM5)"
	@echo "      RUN_CONFIG=$(RUN_CONFIG)"
