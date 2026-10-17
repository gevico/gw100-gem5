# syntax=docker/dockerfile:1

# =============================================================================
# gem5 编译 & 开发环境
#
# 依赖来源：gem5 官方 util/dockerfiles/ubuntu-24.04_all-dependencies/Dockerfile
#           以及 site_scons/site_init.py 中对 SCons >= 3.0.0 的要求
#
# 构建镜像（默认 X86 目标，会预编译 gem5.opt）:
#   docker build -t gem5-dev .
#
# 常用变体:
#   docker build -t gem5-dev --build-arg GEM5_BUILD_TARGET=ARM .
#   docker build -t gem5-dev --build-arg GEM5_BUILD_TARGET=ALL .
#   docker build -t gem5-slim --target base .                 # 仅工具链，不编译
#   docker build -t gem5-dev \
#     --build-arg USER_UID=$(id -u) --build-arg USER_GID=$(id -g) .
#
#   # Docker Desktop 默认仅分配 8 GiB 内存，-j$(nproc) 易 OOM，建议限制并行度
#   docker build -t gem5-dev --build-arg BUILD_JOBS=6 .
#
# 使用预编译二进制（镜像自带 build/）:
#   docker run --rm -it gem5-dev
#   docker run --rm gem5-dev gem5 configs/example/se.py \
#     -c tests/test-progs/hello/bin/x86/linux/hello
#
# 挂载宿主机源码做开发（增量编译结果写回宿主机）:
#   docker run --rm -it -v "$PWD/gem5:/gem5" gem5-dev
#   容器内: scons build/X86/gem5.opt -j$(nproc)
# =============================================================================

ARG UBUNTU_VERSION=24.04
ARG GEM5_BUILD_TARGET=X86

# -----------------------------------------------------------------------------
# Stage 1: base —— 编译依赖与开发工具链
# -----------------------------------------------------------------------------
FROM ubuntu:${UBUNTU_VERSION} AS base

LABEL org.opencontainers.image.title="gem5 development environment"
LABEL org.opencontainers.image.description="Build and development environment for gem5"
LABEL org.opencontainers.image.source="https://github.com/gem5/gem5"
LABEL org.opencontainers.image.licenses="BSD-3-Clause"

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# 前半部分为 gem5 编译必需依赖（对齐官方 all-dependencies 镜像），
# 后半部分为开发与调试工具。
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        scons \
        m4 \
        pkg-config \
        python3-dev \
        python3-venv \
        python3-tk \
        python3-pydot \
        libprotobuf-dev \
        protobuf-compiler \
        libprotoc-dev \
        libgoogle-perftools-dev \
        libboost-all-dev \
        libhdf5-serial-dev \
        libcapstone-dev \
        libpng-dev \
        libelf-dev \
        zlib1g-dev \
        git \
        curl \
        wget \
        ca-certificates \
        cmake \
        doxygen \
        graphviz \
        jq \
        ccache \
        gdb \
        vim \
        less \
        sudo \
        unzip \
        pre-commit \
        mypy \
    && rm -rf /var/lib/apt/lists/*

# pre-commit 在非 root 用户下需要可写缓存目录；/tmp 对所有用户可写
ENV XDG_CACHE_HOME=/tmp

# 非 root 用户，避免挂载宿主机目录后产物属于 root。
# 注意：ubuntu:24.04 基础镜像自带 UID/GID 1000 的 ubuntu 用户，
# 因此这里先检测冲突，能复用则改名，否则新建。
ARG USER_NAME=gem5
ARG USER_UID=1000
ARG USER_GID=1000

RUN set -eu; \
    existing_group="$(getent group "${USER_GID}" | cut -d: -f1)"; \
    if [ -n "${existing_group}" ]; then \
        groupmod --new-name "${USER_NAME}" "${existing_group}"; \
    else \
        groupadd --gid "${USER_GID}" "${USER_NAME}"; \
    fi; \
    existing_user="$(getent passwd "${USER_UID}" | cut -d: -f1)"; \
    if [ -n "${existing_user}" ]; then \
        usermod --login "${USER_NAME}" \
                --home "/home/${USER_NAME}" \
                --move-home \
                --gid "${USER_GID}" \
                "${existing_user}"; \
    else \
        useradd --uid "${USER_UID}" --gid "${USER_GID}" \
                --create-home --shell /bin/bash "${USER_NAME}"; \
    fi; \
    echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/${USER_NAME}"; \
    chmod 0440 "/etc/sudoers.d/${USER_NAME}"; \
    mkdir -p "/home/${USER_NAME}/.ccache"; \
    chown -R "${USER_NAME}:${USER_NAME}" "/home/${USER_NAME}"

# ccache 通过 /usr/lib/ccache 中的 gcc/g++ 代理链接自动生效
ENV PATH=/usr/lib/ccache:${PATH} \
    CCACHE_DIR=/home/${USER_NAME}/.ccache \
    CCACHE_MAXSIZE=10G \
    CCACHE_COMPRESS=1 \
    GEM5_HOME=/gem5

USER ${USER_NAME}
WORKDIR /gem5

# -----------------------------------------------------------------------------
# Stage 2: builder —— 预编译 gem5.opt
# -----------------------------------------------------------------------------
FROM base AS builder

ARG USER_NAME=gem5
ARG USER_UID=1000
ARG USER_GID=1000
ARG GEM5_BUILD_TARGET=X86
# 留空则使用容器内 nproc；内存不足时（如 Docker Desktop 默认 8 GiB）请显式限制
ARG BUILD_JOBS=

COPY --chown=${USER_NAME}:${USER_NAME} gem5/ /gem5/

# ccache 仅作为构建缓存，不写入最终镜像，避免体积膨胀
RUN --mount=type=cache,target=/home/${USER_NAME}/.ccache,uid=${USER_UID},gid=${USER_GID} \
    jobs="${BUILD_JOBS:-$(nproc)}" \
 && echo "==> 编译 build/${GEM5_BUILD_TARGET}/gem5.opt (jobs=${jobs})" \
 && scons build/${GEM5_BUILD_TARGET}/gem5.opt -j"${jobs}"

# -----------------------------------------------------------------------------
# Stage 3: dev —— 最终开发镜像
# -----------------------------------------------------------------------------
FROM base AS dev

ARG USER_NAME=gem5
ARG GEM5_BUILD_TARGET=X86

COPY --from=builder --chown=${USER_NAME}:${USER_NAME} /gem5/ /gem5/

# 提供全局 gem5 命令；挂载源码后仍可显式调用 ./build/<TARGET>/gem5.opt
USER root
RUN ln -sf "/gem5/build/${GEM5_BUILD_TARGET}/gem5.opt" /usr/local/bin/gem5
USER ${USER_NAME}

CMD ["/bin/bash"]
