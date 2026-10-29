# GW100 ARM 裸机程序的构建规则。
#
# 只负责把裸机程序编译成 ELF；运行哪个程序、用什么板卡配置，
# 由仓库根目录的 Makefile 决定（make run）。
#
# 单独使用：
#   make -C baremetal                    # 构建 build/gw100-baremetal/hello.elf
#   make -C baremetal PROGRAM=<名字>     # 构建 <名字>.S
#   make -C baremetal clean

CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
OBJDUMP ?= $(CROSS_COMPILE)objdump
READELF ?= $(CROSS_COMPILE)readelf

PROGRAM ?= hello
SRC ?= $(PROGRAM).S
LDSCRIPT ?= linker.ld
BUILD_DIR ?= ../build/gw100-baremetal
ELF := $(BUILD_DIR)/$(PROGRAM).elf

CFLAGS ?= -nostdlib -nostartfiles -ffreestanding -g
LDFLAGS ?= -Wl,-T,$(LDSCRIPT)

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.PHONY: all clean disasm check help

all: $(ELF)

$(ELF): $(SRC) $(LDSCRIPT) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

$(BUILD_DIR):
	mkdir -p $@

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
	@echo "disasm   反汇编 $(ELF)"
	@echo "check    查看 ELF 架构和入口地址"
	@echo "clean    删除 $(BUILD_DIR)"
	@echo ""
	@echo "变量：PROGRAM=$(PROGRAM)  CROSS_COMPILE=$(CROSS_COMPILE)"
	@echo "      BUILD_DIR=$(BUILD_DIR)"
