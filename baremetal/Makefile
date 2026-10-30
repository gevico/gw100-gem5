# GW100 裸机测试的构建规则。
#
# 只负责编译：公共库（lib/）编译一次，tests/ 下每个 test 链接成一个独立的 ELF。
# 运行哪个 test、用什么板卡和 host 预设，由仓库根目录的 Makefile 决定
# （make run、make test）。架构与约定见 README.md。
#
# 单独使用：
#   make -C baremetal                 # 编译全部 test
#   make -C baremetal TEST=<名字>     # 只编译一个 test
#   make -C baremetal list            # 列出全部 test 名
#   make -C baremetal clean

CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
OBJDUMP ?= $(CROSS_COMPILE)objdump
READELF ?= $(CROSS_COMPILE)readelf

BUILD_DIR ?= ../build/gw100-baremetal
LDSCRIPT := linker.ld

# 一个 test 是 tests/<名字>.c，或者多文件的 tests/<名字>/ 目录
# 用 find 找子目录：GNU make 3.81（macOS 自带）的 $(wildcard tests/*/) 会把文件也匹配进来
TESTS_ALL := $(sort $(basename $(notdir $(wildcard tests/*.c))) \
                    $(notdir $(shell find tests -mindepth 1 -maxdepth 1 -type d)))
TEST ?=
SELECTED := $(if $(TEST),$(TEST),$(TESTS_ALL))

# MMU 关闭，内存按 Device 类型访问：必须对齐（-mstrict-align），且不用 FP/SIMD
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror \
          -ffreestanding -fno-tree-loop-distribute-patterns \
          -mgeneral-regs-only -mstrict-align -Iinclude -MMD -MP
ASFLAGS := -g -Iinclude
LDFLAGS := -nostdlib -nostartfiles -static -Wl,-T,$(LDSCRIPT) -Wl,--no-warn-rwx-segments

LIB_SRCS := $(wildcard lib/*.c lib/*.S)
LIB_OBJS := $(patsubst %,$(BUILD_DIR)/%.o,$(LIB_SRCS))

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.PHONY: all list clean disasm check help

all: $(patsubst %,$(BUILD_DIR)/%.elf,$(SELECTED))

list:
	@printf '%s\n' $(TESTS_ALL)

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.S.o: %.S
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c -o $@ $<

# 每个 test 的源文件、目标文件和链接规则
define TEST_RULES
$(1)_SRCS := $$(if $$(wildcard tests/$(1).c),tests/$(1).c,$$(wildcard tests/$(1)/*.c tests/$(1)/*.S))
$(1)_OBJS := $$(patsubst %,$(BUILD_DIR)/%.o,$$($(1)_SRCS))
$(BUILD_DIR)/$(1).elf: $$($(1)_OBJS) $(LIB_OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o $$@ $$($(1)_OBJS) $(LIB_OBJS) -lgcc
endef
$(foreach t,$(TESTS_ALL),$(eval $(call TEST_RULES,$(t))))

# 以下两个目标作用于 TEST 指定的 test，默认 hello
INSPECT := $(BUILD_DIR)/$(if $(TEST),$(TEST),hello).elf

disasm: $(INSPECT)
	$(OBJDUMP) -d $<

check: $(INSPECT)
	file $<
	$(READELF) -h $< | grep -E 'Machine|Entry'

clean:
	@test -n "$(BUILD_DIR)" || { echo "BUILD_DIR 不能为空"; exit 1; }
	rm -rf -- $(BUILD_DIR)

help:
	@echo "all      编译 TEST 指定的 test，未指定时编译全部"
	@echo "list     列出全部 test 名"
	@echo "disasm   反汇编 TEST 指定的 test（默认 hello）"
	@echo "check    查看 ELF 架构和入口地址"
	@echo "clean    删除 $(BUILD_DIR)"
	@echo ""
	@echo "test：$(TESTS_ALL)"
	@echo "变量：TEST=$(TEST)  CROSS_COMPILE=$(CROSS_COMPILE)"
	@echo "      BUILD_DIR=$(BUILD_DIR)"

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)
