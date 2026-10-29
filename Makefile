# GW100 顶层入口：gem5 仿真、baremetal、环境检查和格式化。
#
# 目录约定：
#   baremetal/   裸机程序源码；它的 Makefile 只负责编译出 ELF
#   运行哪个程序、用哪个 gem5 配置脚本和 workload，都在本文件里选（见"运行配置"）
#
# 常用：
#   make check
#   make all
#   make gem-build
#   make baremetal-build
#   make run
#   make run-only
#   make format
#   make vscode-config
#   make commit-check
#   make clean

SHELL := /bin/sh
VENV ?= .venv
PYTHON ?= $(VENV)/bin/python
# gem-build 在 gem5/ 下执行 scons，默认用绝对路径；覆盖时给绝对路径或 PATH 里的命令。
SCONS ?= $(abspath $(VENV)/bin/scons)
CLANG_FORMAT ?= clang-format

# gem5 嵌入的 Python 与 $(PYTHON) 保持同一版本。gem5 只从环境变量读
# PYTHON_CONFIG（gem5/site_scons/gem5_scons/defaults.py），写成 scons
# 参数不生效，会退回系统默认的 python3-config。
PYTHON_VERSION := $(shell $(PYTHON) -c 'import sys; print("%d.%d" % sys.version_info[:2])' 2>/dev/null)
PYTHON_CONFIG ?= $(if $(PYTHON_VERSION),$(shell command -v python$(PYTHON_VERSION)-config 2>/dev/null))

JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
GEM5_TARGET := build/ARM/gem5.opt
GEM5_CDB := build/ARM/compile_commands.json
GEM5_EXTRAS := $(abspath gem5-ext)

# 运行配置。GEM5_OPTS 给 gem5 本身（写在配置脚本之前），CONFIG_ARGS 给配置脚本。
# 例：make run BAREMETAL_PROGRAM=foo GEM5_OPTS="--debug-flags=PciDevice"
BAREMETAL_DIR := build/gw100-baremetal
BAREMETAL_PROGRAM ?= hello
BAREMETAL_ELF := $(BAREMETAL_DIR)/$(BAREMETAL_PROGRAM).elf
GEM5_CONFIG ?= gem5-ext/configs/run_gw100.py
BOARD ?= arm_vexpress
HOST ?= timing
DEVICE ?= gw100
WORKLOAD ?= baremetal
# 每种组合单独一个输出目录（相对 gem5/），扫描组合时互不覆盖
GEM5_OUTDIR ?= m5out/$(BOARD)-$(HOST)-$(DEVICE)
GEM5_OPTS ?=
CONFIG_ARGS ?=
RUN_GEM5 = cd gem5 && ./$(GEM5_TARGET) --outdir=$(GEM5_OUTDIR) $(GEM5_OPTS) \
	$(abspath $(GEM5_CONFIG)) \
	--board $(BOARD) --host $(HOST) --device $(DEVICE) --workload $(WORKLOAD) \
	$(if $(filter baremetal,$(WORKLOAD)),--kernel $(abspath $(BAREMETAL_ELF))) $(CONFIG_ARGS)

# 代码提示用 clangd；cpptools 只保留调试，关闭它的 IntelliSense 以免重复报错。
CLANGD_CONFIG := .clangd
VSCODE_SETTINGS := .vscode/settings.json
VSCODE_CDB := gem5/$(GEM5_CDB)

.DEFAULT_GOAL := help
.PHONY: all run run-only gem-build baremetal-build vscode-config commit-check clean check format help

all: gem-build baremetal-build

run: gem-build baremetal-build
	$(RUN_GEM5)

run-only: baremetal-build
	$(RUN_GEM5)

gem-build:
	cd gem5 && $(if $(PYTHON_CONFIG),PYTHON_CONFIG=$(PYTHON_CONFIG) )$(SCONS) \
		--ignore-style $(GEM5_TARGET) $(GEM5_CDB) \
		EXTRAS="$(GEM5_EXTRAS)" \
		-j$(JOBS)

vscode-config:
	@printf '%s\n' \
		'# 由 make vscode-config 生成，不要手改。' \
		'CompileFlags:' \
		'  CompilationDatabase: $(patsubst %/,%,$(dir $(VSCODE_CDB)))' \
		> $(CLANGD_CONFIG)
	@echo "已生成 $(CLANGD_CONFIG)"
	@$(PYTHON) tools/vscode_settings.py $(VSCODE_SETTINGS)
	@test -f $(VSCODE_CDB) || echo "[WARN] 还没有 $(VSCODE_CDB)；运行 make gem-build 生成"

baremetal-build:
	$(MAKE) -C baremetal PROGRAM=$(BAREMETAL_PROGRAM) BUILD_DIR=$(abspath $(BAREMETAL_DIR))

commit-check:
	@echo "== 提交检查 =="
	@tools/git-hooks/pre-commit
	@tmp=$$(mktemp); \
	trap 'rm -f "$$tmp"' EXIT; \
	git log -1 --format=%B > "$$tmp"; \
	tools/git-hooks/commit-msg "$$tmp"
	@tools/git-hooks/pre-push --check-dco origin/main..HEAD

clean:
	$(MAKE) -C baremetal clean BUILD_DIR=$(abspath $(BAREMETAL_DIR))

check:
	@echo "== GW100 环境检查 =="
	@fail=0; \
	if [ "$$(uname -s)" = "Darwin" ]; then \
		echo "[OK]   macOS $$(sw_vers -productVersion 2>/dev/null)"; \
	else \
		echo "[WARN] 当前不是 macOS；第一版只保证 macOS"; \
	fi; \
	for tool in make clang clang++ $(CLANG_FORMAT) aarch64-linux-gnu-gcc; do \
		if command -v $$tool >/dev/null 2>&1; then \
			echo "[OK]   $$tool: $$($$tool --version 2>&1 | head -1)"; \
		else \
			echo "[FAIL] 缺少 $$tool"; fail=1; \
		fi; \
	done; \
	if command -v "$(SCONS)" >/dev/null 2>&1; then \
		echo "[OK]   $(SCONS)"; \
	else \
		echo "[FAIL] 缺少 $(SCONS)"; fail=1; \
	fi; \
	if [ -x "$(PYTHON)" ]; then \
		if "$(PYTHON)" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 12) else 1)'; then \
			echo "[OK]   $(PYTHON): $$("$(PYTHON)" --version 2>&1)"; \
		else \
			echo "[FAIL] $(PYTHON) 需要 >= 3.12"; fail=1; \
		fi; \
	else \
		echo "[FAIL] 缺少 $(PYTHON)"; fail=1; \
	fi; \
	if [ -n "$(PYTHON_CONFIG)" ] && [ -x "$(PYTHON_CONFIG)" ]; then \
		echo "[OK]   PYTHON_CONFIG=$(PYTHON_CONFIG)"; \
	else \
		echo "[WARN] 未找到 python$(PYTHON_VERSION)-config；gem5 会退回系统默认的 python3-config"; \
	fi; \
	if [ -n "$(PYTHON_VERSION)" ] && [ -x gem5/$(GEM5_TARGET) ] && command -v otool >/dev/null 2>&1; then \
		if otool -L gem5/$(GEM5_TARGET) | grep -qE 'Versions/$(PYTHON_VERSION)/Python|libpython$(PYTHON_VERSION)'; then \
			echo "[OK]   gem5/$(GEM5_TARGET) 链接 Python $(PYTHON_VERSION)"; \
		else \
			echo "[WARN] gem5/$(GEM5_TARGET) 链接的 Python 不是 $(PYTHON_VERSION)；运行 make gem-build 重新构建"; \
		fi; \
	fi; \
	for d in gem5 gem5-ext baremetal; do \
		if [ -d $$d ]; then echo "[OK]   $$d/"; else echo "[FAIL] 缺少 $$d/"; fail=1; fi; \
	done; \
	if [ -f baremetal/Makefile ]; then \
		echo "[OK]   baremetal/Makefile"; \
	else \
		echo "[FAIL] 缺少 baremetal/Makefile"; fail=1; \
	fi; \
	if [ -f $(VSCODE_CDB) ]; then \
		echo "[OK]   $(VSCODE_CDB)"; \
	else \
		echo "[WARN] 还没有 $(VSCODE_CDB)；运行 make gem-build 生成"; \
	fi; \
	if [ -f $(CLANGD_CONFIG) ]; then \
		echo "[OK]   $(CLANGD_CONFIG)"; \
	else \
		echo "[WARN] 还没有 $(CLANGD_CONFIG)；运行 make vscode-config 生成"; \
	fi; \
	exit $$fail

format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { \
		echo "缺少 $(CLANG_FORMAT)"; exit 1; \
	}
	@find gem5-ext -type f \
		\( -name '*.cc' -o -name '*.hh' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) \
		! -path '*/build/*' ! -name 'isa_defs.hh' -print0 | \
		xargs -0 $(CLANG_FORMAT) -i --style=file:gem5/.clang-format

help:
	@echo "GW100 顶层 Makefile"
	@echo ""
	@echo "  all              构建 gem5 和 baremetal"
	@echo "  run              构建 gem5 和裸机程序，并在 gem5 中运行"
	@echo "  run-only         不重新构建 gem5，直接运行（裸机程序过期时仍会重编）"
	@echo "  gem-build        构建 gem5/$(GEM5_TARGET) 和 compile_commands.json"
	@echo "  baremetal-build  构建 $(BAREMETAL_ELF)（调用 baremetal/Makefile）"
	@echo "  vscode-config    生成 .clangd（指向 gem5 编译数据库），关闭 cpptools IntelliSense"
	@echo "  commit-check     检查暂存文件头、最近提交信息和 DCO"
	@echo "  check            检查 macOS、编译工具链、Python 和目录"
	@echo "  format           用 gem5/.clang-format 格式化 gem5-ext 的 C++ 文件"
	@echo "  clean            清理裸机构建产物（不删 gem5/build）"
	@echo "  help             显示本帮助"
	@echo ""
	@echo "运行配置（可在命令行覆盖）："
	@echo "  BAREMETAL_PROGRAM=$(BAREMETAL_PROGRAM)   编译 baremetal/<名字>.S"
	@echo "  GEM5_CONFIG=$(GEM5_CONFIG)"
	@echo "  BOARD=$(BOARD)   板卡（gem5-ext/configs/gw100/boards/）"
	@echo "  HOST=$(HOST)   host 预设：timing 或 o3（gem5-ext/configs/gw100/hosts/）"
	@echo "  DEVICE=$(DEVICE)   设备预设（gem5-ext/configs/gw100/devices/）"
	@echo "  GEM5_OUTDIR=$(GEM5_OUTDIR)   输出目录，相对 gem5/"
	@echo "  WORKLOAD=$(WORKLOAD)   baremetal 或 linux"
	@echo "  GEM5_OPTS=$(GEM5_OPTS)   gem5 本身的选项，如 --debug-flags=..."
	@echo "  CONFIG_ARGS=$(CONFIG_ARGS)   追加给配置脚本的参数"
