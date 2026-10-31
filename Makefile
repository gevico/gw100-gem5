# GW100 顶层入口：gem5 仿真、baremetal、环境检查和格式化。
#
# 目录约定：
#   baremetal/   裸机测试：公共库 lib/ 加上 tests/ 下的独立 test；它的 Makefile 只负责编译
#   运行哪个 test、用哪个 gem5 配置脚本和 workload，都在本文件里选（见"运行配置"）
#
# 常用：
#   make check
#   make all
#   make gem-build
#   make baremetal-build
#   make run
#   make test
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
GEM5_BIN := gem5/$(GEM5_TARGET)

# 只有这些输入比 gem5.opt 新时才调用 scons。scons 即使没有任何改动也会重新链接
# gem5.opt（耗时一两分钟），所以不能每次都交给它判断。配置脚本 gem5-ext/configs/
# 在运行时加载，不需要重新构建。
GEM5_INPUTS := $(shell find gem5-ext/SConscript gem5-ext/src -type f \
	\( -name '*.cc' -o -name '*.hh' -o -name '*.py' -o -name SConscript \) 2>/dev/null)
GEM5_HEAD := $(shell git -C gem5 rev-parse HEAD 2>/dev/null)
GEM5_HEAD_STAMP := gem5/build/ARM/.gw100-gem5-head
GEM5_SCONS = cd gem5 && $(if $(PYTHON_CONFIG),PYTHON_CONFIG=$(PYTHON_CONFIG) )$(SCONS) \
	--ignore-style $(GEM5_TARGET) $(GEM5_CDB) \
	EXTRAS="$(GEM5_EXTRAS)" \
	-j$(JOBS)

# 运行配置。GEM5_OPTS 给 gem5 本身（写在配置脚本之前），CONFIG_ARGS 给配置脚本。
# 例：make run TEST=pcie_regs GEM5_OPTS="--debug-flags=Gw100Pcie"
BAREMETAL_DIR := build/gw100-baremetal
TEST ?= hello
BAREMETAL_ELF := $(BAREMETAL_DIR)/$(TEST).elf
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

# 批量测试配置。例：make test TESTS="pcie_*" HOSTS=o3
HOSTS ?= timing atomic
TESTS ?=
TEST_JOBS ?= $(JOBS)
TEST_TIMEOUT ?= 300
TEST_MAX_TICKS ?= 10000000000
TEST_OUTDIR ?= gem5/m5out/tests
BMTEST = $(PYTHON) tools/bmtest.py --gem5 gem5/$(GEM5_TARGET) --config $(GEM5_CONFIG) \
	--baremetal-dir baremetal --build-dir $(BAREMETAL_DIR) --outdir $(TEST_OUTDIR) \
	--board $(BOARD) --device $(DEVICE) --hosts "$(HOSTS)" --tests "$(TESTS)" \
	-j $(TEST_JOBS) --timeout $(TEST_TIMEOUT) --max-ticks $(TEST_MAX_TICKS)

# 代码提示用 clangd；cpptools 只保留调试，关闭它的 IntelliSense 以免重复报错。
CLANGD_CONFIG := .clangd
VSCODE_SETTINGS := .vscode/settings.json
VSCODE_CDB := gem5/$(GEM5_CDB)

.DEFAULT_GOAL := help
.PHONY: all run test gem-build FORCE baremetal-build vscode-config commit-check clean check format help

all: gem-build baremetal-build

run: gem-build baremetal-build
	$(RUN_GEM5)

test: gem-build baremetal-build
	$(BMTEST)

gem-build: $(GEM5_BIN)

# 改了 PYTHON_CONFIG 等构建选项、或怀疑构建状态不一致时，用 make -B gem-build 强制构建
$(GEM5_BIN): $(GEM5_INPUTS) $(GEM5_HEAD_STAMP)
	$(GEM5_SCONS)
	@touch $@

# 记录 gem5 子模块的提交号；内容不变时不改写，文件时间也就不变
$(GEM5_HEAD_STAMP): FORCE
	@mkdir -p $(@D)
	@echo '$(GEM5_HEAD)' | cmp -s - $@ || echo '$(GEM5_HEAD)' > $@

vscode-config:
	@printf '%s\n' \
		'# 由 make vscode-config 生成，不要手改。' \
		'CompileFlags:' \
		'  CompilationDatabase: $(patsubst %/,%,$(dir $(VSCODE_CDB)))' \
		> $(CLANGD_CONFIG)
	@printf '%s\n' \
		'# 由 make vscode-config 生成，不要手改。裸机代码没有编译数据库，直接给出编译参数。' \
		'CompileFlags:' \
		'  CompilationDatabase: None' \
		'  Add: [-xc, -std=c11, --target=aarch64-none-elf, -ffreestanding, -I$(abspath baremetal/include)]' \
		> baremetal/$(CLANGD_CONFIG)
	@echo "已生成 $(CLANGD_CONFIG)、baremetal/$(CLANGD_CONFIG)"
	@$(PYTHON) tools/vscode_settings.py $(VSCODE_SETTINGS)
	@test -f $(VSCODE_CDB) || echo "[WARN] 还没有 $(VSCODE_CDB)；运行 make gem-build 生成"

baremetal-build:
	$(MAKE) -C baremetal BUILD_DIR=$(abspath $(BAREMETAL_DIR))

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
	@echo "  run              构建 gem5 和裸机测试，在 gem5 中运行 TEST 指定的一个"
	@echo "  test             构建 gem5 和裸机测试，批量运行 TESTS × HOSTS 并汇总"
	@echo "  gem-build        gem5-ext 源码或 gem5 子模块有变化时，构建 gem5/$(GEM5_TARGET) 和编译数据库；"
	@echo "                   make -B gem-build 强制构建（改了构建选项时用）"
	@echo "  baremetal-build  构建全部裸机测试（调用 baremetal/Makefile）"
	@echo "  vscode-config    生成 .clangd 和 baremetal/.clangd，关闭 cpptools IntelliSense"
	@echo "  commit-check     检查暂存文件头、最近提交信息和 DCO"
	@echo "  check            检查 macOS、编译工具链、Python 和目录"
	@echo "  format           用 gem5/.clang-format 格式化 gem5-ext 的 C++ 文件"
	@echo "  clean            清理裸机构建产物（不删 gem5/build）"
	@echo "  help             显示本帮助"
	@echo ""
	@echo "运行配置（可在命令行覆盖）："
	@echo "  TEST=$(TEST)   run 运行的裸机测试（baremetal/tests/<名字>.c）"
	@echo "  GEM5_CONFIG=$(GEM5_CONFIG)"
	@echo "  BOARD=$(BOARD)   板卡（gem5-ext/configs/gw100/boards/）"
	@echo "  HOST=$(HOST)   host 预设：timing、atomic 或 o3（gem5-ext/configs/gw100/hosts/）"
	@echo "  DEVICE=$(DEVICE)   设备预设（gem5-ext/configs/gw100/devices/）"
	@echo "  GEM5_OUTDIR=$(GEM5_OUTDIR)   输出目录，相对 gem5/"
	@echo "  WORKLOAD=$(WORKLOAD)   baremetal 或 linux"
	@echo "  GEM5_OPTS=$(GEM5_OPTS)   gem5 本身的选项，如 --debug-flags=..."
	@echo "  CONFIG_ARGS=$(CONFIG_ARGS)   追加给配置脚本的参数"
	@echo ""
	@echo "批量测试配置："
	@echo "  TESTS=$(TESTS)   空格分隔的 test 名，支持通配；空表示全部"
	@echo "  HOSTS=$(HOSTS)   每个 test 在这些 host 预设下各跑一遍"
	@echo "  TEST_JOBS=$(TEST_JOBS)  TEST_TIMEOUT=$(TEST_TIMEOUT) 秒  TEST_MAX_TICKS=$(TEST_MAX_TICKS)"
	@echo "  TEST_OUTDIR=$(TEST_OUTDIR)   每次运行的输出在 <TEST_OUTDIR>/<host>/<test>/"
