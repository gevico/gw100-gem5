"""张量指令汇编器：构造指令、编码成 128 字节，并打包成程序和命令记录。

编码格式全部来自生成的 isa_defs.py（源头是 spec-src/isa/isa-v0.yaml）。
"""

import struct
from dataclasses import dataclass, field

from .dtypes import elem_bits
from .isa_defs import (COMMAND_RECORD, COMPLETION_RECORD, DTYPES, FLAGS, HEADER,
                       INST_BYTES, LOCAL_TILE, OPCODES, OPERAND, OPERAND_FIELDS,
                       PROGRAM, SUBOPS)

_FMT = {"u8": "<B", "u16": "<H", "u32": "<I", "u64": "<Q", "i32": "<i", "f32": "<f"}


@dataclass
class Tensor:
    """张量操作数。shape 按逻辑顺序给出（最外层在前，最多 4 维）；strides 以字节为单位，
    省略时按行优先紧密排列。"""

    addr: int
    shape: tuple
    dtype: str
    strides: tuple = None

    def __post_init__(self):
        self.shape = tuple(int(s) for s in self.shape)
        if not 1 <= len(self.shape) <= 4:
            raise ValueError(f"张量最多 4 维，收到 {self.shape}")
        if any(s < 1 or s > 0xFFFF for s in self.shape):
            raise ValueError(f"每一维的尺寸必须在 1..65535 之间，收到 {self.shape}")
        if self.strides is None:
            self.strides = self.contiguous_strides()
        self.strides = tuple(int(s) for s in self.strides)
        if len(self.strides) != len(self.shape):
            raise ValueError("strides 的维数必须与 shape 相同")

    def contiguous_strides(self):
        bits = elem_bits(self.dtype)
        strides = []
        acc_bits = bits
        for s in reversed(self.shape):
            strides.append(acc_bits // 8 if bits >= 8 else 0)
            acc_bits *= s
        if bits < 8:  # INT4：d0 紧密排列，外层按字节计
            strides = [0]
            acc = self.shape[-1] * bits // 8
            for s in reversed(self.shape[:-1]):
                strides.append(acc)
                acc *= s
        return tuple(reversed(strides))

    @property
    def numel(self):
        n = 1
        for s in self.shape:
            n *= s
        return n

    def broadcast(self, shape):
        """返回一个按 shape 广播读取的视图（新增或尺寸为 1 的维度 stride 置 0）。"""
        shape = tuple(shape)
        pad = len(shape) - len(self.shape)
        src_shape = (1,) * pad + self.shape
        src_strides = (0,) * pad + self.strides
        strides = tuple(0 if s == 1 and t != 1 else st
                        for s, t, st in zip(src_shape, shape, src_strides))
        return Tensor(self.addr, shape, self.dtype, strides)

    def encode_fields(self):
        """返回 (addr, size[4], stride[4])，d0 为最内层。"""
        size = list(reversed(self.shape)) + [1] * (4 - len(self.shape))
        stride = list(reversed(self.strides)) + [0] * (4 - len(self.strides))
        return self.addr, size, stride


@dataclass
class Inst:
    opcode: str
    subop: int = 0
    flags: int = 0
    operands: list = field(default_factory=lambda: [None, None, None])
    dtypes: list = field(default_factory=lambda: ["FP32", "FP32", "FP32"])
    wait: tuple = None        # (counter, value)：开始前等待 counter >= value
    signal: tuple = None      # (counter, tile)：完成后 counter += 1；tile 为 None 表示本 Tile
    imm: float = 0.0
    group_size: int = 0
    aux_addr: int = 0

    def encode(self):
        b = bytearray(INST_BYTES)

        def put(name, value):
            off, typ = HEADER[name]
            struct.pack_into(_FMT[typ], b, off, value)

        put("opcode", OPCODES[self.opcode]["code"])
        put("subop", self.subop)
        put("flags", self.flags)
        for name, dt in zip(("dtype_a", "dtype_b", "dtype_c"), self.dtypes):
            put(name, DTYPES[dt]["code"])
        if self.wait is not None:
            put("wait_en", 1)
            put("wait_cnt", self.wait[0])
            put("wait_val", self.wait[1])
        if self.signal is not None:
            cnt, tile = self.signal
            put("sig_en", 1)
            put("sig_cnt", cnt)
            put("sig_tile", LOCAL_TILE if tile is None else tile)
        else:
            put("sig_tile", LOCAL_TILE)
        put("imm", float(self.imm))
        put("group_size", self.group_size)
        put("aux_addr", self.aux_addr)
        for k, t in enumerate(self.operands):
            if t is None:
                continue
            base = OPERAND["base"] + k * OPERAND["bytes"]
            addr, size, stride = t.encode_fields()
            struct.pack_into("<Q", b, base + OPERAND_FIELDS["addr"][0], addr)
            struct.pack_into("<4H", b, base + OPERAND_FIELDS["size"][0], *size)
            struct.pack_into("<4i", b, base + OPERAND_FIELDS["stride"][0], *stride)
        return bytes(b)


# ---------------------------------------------------------------- 指令构造函数

def nop(**kw):
    return Inst("NOP", **kw)


def end(**kw):
    return Inst("END", **kw)


def sync(wait=None, signal=None):
    return Inst("SYNC", wait=wait, signal=signal)


def dma_copy(src, dst, **kw):
    if src.dtype != dst.dtype:
        raise ValueError("DMA_COPY 的源和目的数据类型必须相同")
    if src.numel != dst.numel:
        raise ValueError(f"DMA_COPY 元素个数不一致：{src.numel} vs {dst.numel}")
    return Inst("DMA_COPY", operands=[src, None, dst],
                dtypes=[src.dtype, src.dtype, dst.dtype], **kw)


def vec_binary(op, a, b, c, **kw):
    if a.numel != c.numel or b.numel != c.numel:
        raise ValueError("VEC_BINARY 的三个操作数元素个数必须相同（广播请先用 Tensor.broadcast）")
    return Inst("VEC_BINARY", subop=SUBOPS["VEC_BINARY"][op], operands=[a, b, c],
                dtypes=[a.dtype, b.dtype, c.dtype], **kw)


def vec_scalar(op, a, imm, c, **kw):
    if a.numel != c.numel:
        raise ValueError("VEC_SCALAR 的源和目的元素个数必须相同")
    return Inst("VEC_SCALAR", subop=SUBOPS["VEC_SCALAR"][op], operands=[a, None, c],
                dtypes=[a.dtype, a.dtype, c.dtype], imm=imm, **kw)


# ---------------------------------------------------------------- 程序与 Host 接口记录

class Program:
    """一串 (tile_mask, 指令)。tile_mask 的第 i 位对应 Compute Tile i。"""

    def __init__(self):
        self.entries = []

    def add(self, tiles, inst):
        if isinstance(tiles, int):
            tiles = [tiles]
        mask = 0
        for t in tiles:
            mask |= 1 << t
        self.entries.append((mask, inst))
        return self

    def tiles(self):
        mask = 0
        for m, _ in self.entries:
            mask |= m
        return mask

    def encode(self):
        out = bytearray(struct.pack("<IHHII", PROGRAM["magic"], 1, 0, len(self.entries), 0))
        for mask, inst in self.entries:
            out += struct.pack("<Q", mask) + inst.encode()
        assert len(out) == PROGRAM["header_bytes"] + len(self.entries) * PROGRAM["entry_bytes"]
        return bytes(out)


def _pack_record(layout, values):
    b = bytearray(32)
    for name, (off, typ) in layout.items():
        struct.pack_into(_FMT[typ], b, off, values.get(name, 0))
    return bytes(b)


def _unpack_record(layout, raw):
    return {name: struct.unpack_from(_FMT[typ], raw, off)[0] for name, (off, typ) in layout.items()}


def command_record(program_addr, program_bytes, completion_addr, tag=0, flags=0):
    return _pack_record(COMMAND_RECORD, {
        "program_addr": program_addr, "program_bytes": program_bytes,
        "flags": flags, "completion_addr": completion_addr, "tag": tag})


def parse_completion(raw):
    return _unpack_record(COMPLETION_RECORD, raw)


ACCUMULATE = FLAGS["ACCUMULATE"]
