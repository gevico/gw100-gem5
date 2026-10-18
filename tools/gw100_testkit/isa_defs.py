"""由 tools/isagen/gen.py 根据 spec-src/isa/isa.yaml 生成，请勿手改。"""

VERSION = '0.3'
INST_BYTES = 128
LOCAL_TILE = 0xffff

DTYPES = {
    'INT8': {'code': 0, 'bits': 8},
    'UINT8': {'code': 1, 'bits': 8},
    'INT16': {'code': 2, 'bits': 16},
    'INT32': {'code': 3, 'bits': 32},
    'FP16': {'code': 4, 'bits': 16},
    'BF16': {'code': 5, 'bits': 16},
    'FP32': {'code': 6, 'bits': 32},
    'INT4': {'code': 7, 'bits': 4},
}

OPCODES = {
    'NOP': {'code': 0x00, 'unit': 'ctrl', 'status': 'implemented'},
    'END': {'code': 0x01, 'unit': 'ctrl', 'status': 'implemented'},
    'SYNC': {'code': 0x02, 'unit': 'ctrl', 'status': 'implemented'},
    'DMA_COPY': {'code': 0x10, 'unit': 'dma', 'status': 'implemented'},
    'MATMUL': {'code': 0x20, 'unit': 'me', 'status': 'planned'},
    'VEC_BINARY': {'code': 0x30, 'unit': 've', 'status': 'implemented'},
    'VEC_UNARY': {'code': 0x31, 'unit': 've', 'status': 'planned'},
    'VEC_REDUCE': {'code': 0x32, 'unit': 've', 'status': 'planned'},
    'VEC_SCALAR': {'code': 0x33, 'unit': 've', 'status': 'implemented'},
}

SUBOPS = {
    'VEC_BINARY': {'ADD': 0, 'SUB': 1, 'MUL': 2, 'MAX': 3, 'MIN': 4},
    'VEC_UNARY': {'COPY': 0, 'NEG': 1, 'EXP': 2, 'RSQRT': 3, 'RECIP': 4, 'SILU': 5, 'SQUARE': 6},
    'VEC_REDUCE': {'SUM': 0, 'MAX': 1},
    'VEC_SCALAR': {'ADD': 0, 'MUL': 1, 'MAX': 2},
}

FLAGS = {'ACCUMULATE': 1}

HEADER = {
    'opcode': (0, 'u8'),
    'subop': (1, 'u8'),
    'flags': (2, 'u8'),
    'dtype_a': (3, 'u8'),
    'dtype_b': (4, 'u8'),
    'dtype_c': (5, 'u8'),
    'wait_en': (8, 'u8'),
    'wait_cnt': (9, 'u8'),
    'wait_val': (10, 'u16'),
    'sig_en': (12, 'u8'),
    'sig_cnt': (13, 'u8'),
    'sig_tile': (14, 'u16'),
    'imm': (16, 'f32'),
    'group_size': (20, 'u16'),
    'aux_addr': (24, 'u64'),
}

OPERAND = {'count': 3, 'base': 32, 'bytes': 32, 'dims': 4}
OPERAND_FIELDS = {
    'addr': (0, 'u64', 1),
    'size': (8, 'u16', 4),
    'stride': (16, 'i32', 4),
}

PROGRAM = {'magic': 0x30315747, 'header_bytes': 16, 'entry_bytes': 136}
MGMT_REGS = {'CMD_ADDR': 0x00, 'DOORBELL': 0x08, 'STATUS': 0x0c, 'VERSION': 0x10}
COMMAND_RECORD = {'program_addr': (0, 'u64'), 'program_bytes': (8, 'u32'), 'flags': (12, 'u32'), 'completion_addr': (16, 'u64'), 'tag': (24, 'u64')}
COMPLETION_RECORD = {'status': (0, 'u32'), 'tag': (8, 'u64'), 'start_tick': (16, 'u64'), 'end_tick': (24, 'u64')}
