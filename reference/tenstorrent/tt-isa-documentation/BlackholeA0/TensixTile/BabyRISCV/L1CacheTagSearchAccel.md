# L1 Cache Tag Search Accelerator

A small hardware block exists within each Tensix tile for the purpose of accelerating certain array search operations. Said search operations are motivated by the potential use-case of L1 being a software-managed cache, hence the name "L1 Cache Tag Search Accelerator". This hardware block is only usable by RISCV B.

## Configuration

Subsequent pseudocode assumes the following global variable describing the configuration of the hardware block:
```c
struct {
  bool Search_Enable;
  bool Tag_alloc;
  bool Tag_inv;
  bool Tag_inv_all;
  uint2_t Tag_Width;
  uint64_t Tag_Value;
  uint17_t Start_Addr;
  uint17_t End_Addr;
  uint17_t Valid_bit_section_start_addr;
  uint17_t Valid_bit_section_end_addr;
  bool Data_Valid_chk;
  uint17_t Data_Valid_bit_section_start_addr;
  uint24_t Data_Valid_offset;
} LatchedConfig;
```

Software cannot write to `LatchedConfig` directly; instead it writes to Tensix backend configuration (referred to as `Config`), and whenever software _changes_ the value of any of:
* `Config.L1_CACHE_TAG_SEARCH_ACCEL_Search_Enable`
* `Config.L1_CACHE_TAG_SEARCH_ACCEL_Tag_alloc`
* `Config.L1_CACHE_TAG_SEARCH_ACCEL_Tag_inv`
* `Config.L1_CACHE_TAG_SEARCH_ACCEL_Tag_inv_all`
* `Config.L1_CACHE_TAG_SEARCH_ACCEL_Data_Valid_chk`

Hardware will latch all of the relevant configuration:
```c
LatchedConfig.Search_Enable                     = Config.L1_CACHE_TAG_SEARCH_ACCEL_Search_Enable;
LatchedConfig.Tag_alloc                         = Config.L1_CACHE_TAG_SEARCH_ACCEL_Tag_alloc;
LatchedConfig.Tag_inv                           = Config.L1_CACHE_TAG_SEARCH_ACCEL_Tag_inv;
LatchedConfig.Tag_inv_all                       = Config.L1_CACHE_TAG_SEARCH_ACCEL_Tag_inv_all;
LatchedConfig.Tag_Width                         = Config.L1_CACHE_TAG_SEARCH_ACCEL_Tag_Width;
LatchedConfig.Tag_Value                         = Config.L1_CACHE_TAG_SEARCH_ACCEL_Tag_Value_low + ((uint64_t)Config.L1_CACHE_TAG_SEARCH_ACCEL_Tag_Value_high << 32);
LatchedConfig.Start_Addr                        = Config.L1_CACHE_TAG_SEARCH_ACCEL_Start_Addr;
LatchedConfig.End_Addr                          = Config.L1_CACHE_TAG_SEARCH_ACCEL_End_Addr;
LatchedConfig.Valid_bit_section_start_addr      = Config.L1_CACHE_TAG_SEARCH_ACCEL_Valid_bit_section_start_addr;
LatchedConfig.Valid_bit_section_end_addr        = Config.L1_CACHE_TAG_SEARCH_ACCEL_Valid_bit_section_end_addr;
LatchedConfig.Data_Valid_chk                    = Config.L1_CACHE_TAG_SEARCH_ACCEL_Data_Valid_chk;
LatchedConfig.Data_Valid_bit_section_start_addr = Config.L1_CACHE_TAG_SEARCH_ACCEL_Data_Valid_bit_section_start_addr;
LatchedConfig.Data_Valid_offset                 = Config.L1_CACHE_TAG_SEARCH_ACCEL_Data_Valid_offset;
```

## Tag search operation

Provided that all of:
* `LatchedConfig.Search_Enable == true`
* `LatchedConfig.Tag_inv_all == false`
* `LatchedConfig.Data_Valid_chk == false`
* RISCV B performs a read against address `LatchedConfig.Start_Addr * 16` (or some other address in the same aligned 16-byte range)
* That read is not an L0 data cache hit

Then the read will not be sent to L1, and instead the result of the read (as returned to RISCV B, and used to populate its L0 data cache) will be the result of the following logic:

```c
switch (LatchedConfig.Tag_Width) {
case 0: return TagSearch<uint8_t>();
case 1: return TagSearch<uint16_t>();
case 2: return TagSearch<uint32_t>();
case 3: return TagSearch<uint64_t>();
}

template <typename T>
uint32_t TagSearch() {
  const T* Tags = (const T*)(LatchedConfig.Start_Addr * 16);
  const T* TagsEnd = (const T*)((LatchedConfig.End_Addr + 1) * 16);
  uint64_t* Valids = (uint64_t*)(LatchedConfig.Valid_bit_section_start_addr * 16);

  for (size_t i = 0; Tags + i != TagsEnd; ++i) {
    if (Tags[i] == (T)LatchedConfig.Tag_Value) {
      if (Valids[i / 64].Bit[i % 64]) {
        // Tag found, and it was valid.
        if (LatchedConfig.Tag_inv) {
          Valids[i / 64].Bit[i % 64] = false;
        }
        return 1 + i;
      } else {
        // Tag found, but it was invalid. Abort the search.
        break;
      }
    }
  }

  if (LatchedConfig.Tag_alloc) {
    // Search the validity array for the first unset bit, and return its index.
    uint64_t* ValidsEnd = (uint64_t*)((LatchedConfig.Valid_bit_section_end_addr + 1) * 16);
    for (size_t i = 0; Valids + i != ValidsEnd; ++i) {
      uint64_t ValidWord = Valids[i];
      for (size_t j = 0; j < 64; ++j) {
        if (!ValidWord.Bit[j]) {
          return 0x80000001 + i * 64 + j;
        }
      }
    }
    // Everything is valid, so choose a random index and return it.
    return 0x80000001 + rand() % ((ValidsEnd - Valids) * 64);
  } else {
    return 0;
  }
}
```

## Invalidate all operation

Provided that all of:
* `LatchedConfig.Tag_inv_all == true`
* RISCV B performs a read against address `LatchedConfig.Valid_bit_section_start_addr * 16` (or some other address in the same aligned 16-byte range)
* That read is not an L0 data cache hit

Then the read will not be sent to L1, and instead the result of the read (as returned to RISCV B, and used to populate its L0 data cache) will be the result of the following logic:

```c
uint64_t* Valids = (uint64_t*)(LatchedConfig.Valid_bit_section_start_addr * 16);
uint64_t* ValidsEnd = (uint64_t*)((LatchedConfig.Valid_bit_section_end_addr + 1) * 16);
for (size_t i = 0; Valids + i != ValidsEnd; ++i) {
  Valids[i] = 0;
}
return 0;
```

## Bit vector query operation

Provided that all of:
* `LatchedConfig.Data_Valid_chk == true`
* `LatchedConfig.Tag_inv_all == false`
* RISCV B performs a read against address `LatchedConfig.Data_Valid_bit_section_start_addr * 16` (or some other address in the same aligned 16-byte range)
* That read is not an L0 data cache hit

Then the read will not be sent to L1, and instead the result of the read (as returned to RISCV B, and used to populate its L0 data cache) will be the result of the following logic:

```c
const uint64_t* BitVector = (const uint64_t*)(LatchedConfig.Data_Valid_bit_section_start_addr * 16);
return BitVector[LatchedConfig.Data_Valid_offset / 64].Bit[LatchedConfig.Data_Valid_offset % 64];
```
