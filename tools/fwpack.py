"""
@file  fwpack.py
@brief 把固件的二进制打包成带镜像头的固件镜像

输入是固件 ELF 和它从装载区起始处开始的原始二进制（objcopy -O binary），
前 64 B 是链接时占位的镜像头。本工具按 spec-mgmt-tile §8.2 填写镜像头：
MAGIC、版本、长度、入口偏移、固件版本和偏移 0x40 起内容的 SHA-256，并在
末尾补 0 使总长度为 8 的整数倍（驱动按 8 字节写入启动窗口，spec-pcie
§6.4）。

用法：
    python3 tools/fwpack.py --elf fw.elf --bin fw.bin --out fw.img \\
        --version 0x00010000

@see spec-mgmt-tile, spec-pcie
"""

import argparse
import hashlib
import pathlib
import struct
import sys

MAGIC = 0x57465747          # "GWFW"
HDR_VERSION = 1
HDR_SIZE = 64
LOAD_BASE = 0x1000_0000     # 装载区的本地地址
LOAD_MAX = 1 << 20          # mgmt.fw_load_max


def elf_entry(path: pathlib.Path) -> int:
    """读取 ELF64 小端文件的入口地址 e_entry。"""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError(f"{path} 不是 ELF64 小端文件")
    return struct.unpack_from("<Q", data, 24)[0]


def pack(raw: bytes, entry: int, version: int) -> bytes:
    """按 §8.2 填写镜像头并补齐长度，返回完整镜像。"""
    if len(raw) < HDR_SIZE:
        raise ValueError("二进制短于镜像头")
    body = bytearray(raw)
    body += b"\0" * (-len(body) % 8)
    if len(body) > LOAD_MAX:
        raise ValueError(f"镜像 {len(body)} B 超过装载区 {LOAD_MAX} B")
    entry_off = entry - LOAD_BASE
    if entry_off < HDR_SIZE or entry_off % 4 or entry_off >= len(body):
        raise ValueError(f"入口偏移 {entry_off:#x} 不合法")
    digest = hashlib.sha256(bytes(body[HDR_SIZE:])).digest()
    hdr = struct.pack("<IHHIII12s32s", MAGIC, HDR_VERSION, HDR_SIZE,
                      len(body), entry_off, version, b"\0" * 12, digest)
    body[:HDR_SIZE] = hdr
    return bytes(body)


def main() -> int:
    """解析参数，生成镜像。"""
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    _ = p.add_argument("--elf", type=pathlib.Path, required=True)
    _ = p.add_argument("--bin", type=pathlib.Path, required=True)
    _ = p.add_argument("--out", type=pathlib.Path, required=True)
    _ = p.add_argument("--version", type=lambda s: int(s, 0), default=0)
    args = p.parse_args()
    try:
        img = pack(args.bin.read_bytes(), elf_entry(args.elf), args.version)
    except ValueError as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        return 1
    args.out.write_bytes(img)
    print(f"已生成 {args.out}：{len(img)} B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
