# Baseline Data Movement Service

The Tenstorrent code running on Ethernet tiles provides a baseline data movement service, which host software can make use of. In particular, this is how host software is expected to _initially_ communicate with any ASICs which are only reachable via Ethernet. Host software might then continue to use the baseline service, or it might use the baseline service to push device code to Ethernet tiles and have that code implement a more bespoke service which the host software switches to using.

## Request types

The baseline data movement service supports a few different kinds of read request (device → host), and a few different kinds of write request (host → device), each with different address and length constraints:

|Request type|Address Constraints|Length Constraints|Response type|
|---|---|---|---|
|`CMD_RD_REQ`|4-byte aligned|Exactly 4 bytes|`CMD_RD_DATA`|
|<code>CMD_RD_REQ</code><br/><code>&#124;&nbsp;CMD_DATA_BLOCK</code>|16-byte aligned or<br/>32-byte aligned (†)|≤ 1024 bytes<br/>and multiple of 4 bytes|`CMD_RD_DATA`|
|<code>CMD_RD_REQ</code><br/><code>&#124;&nbsp;CMD_DATA_BLOCK</code><br/><code>&#124;&nbsp;CMD_DATA_BLOCK_DRAM</code>|Host: 32-byte aligned, pinned<br/>Device: 16-byte aligned or<br/>32-byte aligned (†)|< 4 GiB<br/>and multiple of 4 bytes|`CMD_RD_DATA`|
|`CMD_WR_REQ`|4-byte aligned|Exactly 4 bytes|Counter only|
|<code>CMD_WR_REQ</code><br/><code>&#124;&nbsp;CMD_DATA_BLOCK</code><br/><code>&#124;&nbsp;CMD_MOD</code>|4-byte aligned|≤ 1012 bytes<br/>and multiple of 4 bytes|Counter only|
|<code>CMD_WR_REQ</code><br/><code>&#124;&nbsp;CMD_DATA_BLOCK</code>|16-byte aligned or<br/>32-byte aligned (†)|≤ 1024 bytes<br/>and multiple of 4 bytes|Counter only|
|<code>CMD_WR_REQ</code><br/><code>&#124;&nbsp;CMD_DATA_BLOCK</code><br/><code>&#124;&nbsp;CMD_DATA_BLOCK_DRAM</code><br/><code>&#124;&nbsp;CMD_MOD</code>|Host: 32-byte aligned, pinned<br/>Device: 4-byte aligned|Total: < 3.95 GiB<br/>Per page: ≤ 1012 bytes<br/>and multiple of 4 bytes|Counter only|
|<code>CMD_WR_REQ</code><br/><code>&#124;&nbsp;CMD_DATA_BLOCK</code><br/><code>&#124;&nbsp;CMD_DATA_BLOCK_DRAM</code>|Host: 32-byte aligned, pinned<br/>Device: 16-byte aligned or<br/>32-byte aligned (†)|< 4 GiB<br/>and multiple of 4 bytes|Counter only|

> (†) 16-byte aligned if the address references a Tensix tile or an Ethernet tile, 32-byte aligned for other types of tile. Because of the alignment constraint, this type of request is rarely appropriate for MMIO addresses in Tensix / Ethernet tiles.

All requests support the `CMD_ORDERED` flag (see [Ordering](#ordering)), and the `CMD_NOC_ID` flag (if set, the final NoC hop uses NoC #1, otherwise it uses NoC #0).

## Addressing

Requests contain 76 bits of device address:
* 48 bits as per [`NOC_TARG_ADDR`](../NoC/MemoryMap.md#noc_targ_addr-and-noc_ret_addr):
  * 36 bits of memory address (the high 4 bits of which are usually zero)
  * 6 bits [NoC X](../NoC/Coordinates.md)
  * 6 bits [NoC Y](../NoC/Coordinates.md)
* 12 bits to specify the position of the chip (i.e. ASIC) within a shelf / server:
  * 6 bits chip X
  * 6 bits chip Y
* 16 bits to specify the rack position:
  * 8 bits rack X
  * 8 bits rack Y

The [tt-topology](https://github.com/tenstorrent/tt-topology) tool should be used to assign chip X/Y and rack X/Y.

Requests with the `CMD_DATA_BLOCK_DRAM` flag set contain a 32-bit host address, which addresses the 4 GiB region of [NoC to Host address space in the PCI Express tile](../PCIExpressTile/README.md#noc-to-host-64-gib) starting at `0x8_0000_0000`.

## Queues

The service is presented to host software as a pair of queues: one submission queue (SQ), and one completion queue (CQ). Each queue has a capacity of four entries (requests in the submission queue, responses in the completion queue). Every Ethernet tile presents a pair of queues, and host software can use _any_ Ethernet tile: the service will route data around as necessary (using a combination of NoC and Ethernet) to get from the initial Ethernet tile to the final destination tile(s), and then likewise route the response back to the initial Ethernet tile.

The queues exist in the L1 of each Ethernet tile, as an instance of `eth_base_firmware_queues_t`:

```c
typedef struct eth_base_firmware_queues_t {
  uint64_t latency_counter[16];
  eth_queue_t sq; // Contains requests, for host -> Ethernet tile.
  eth_queue_t reserved;
  eth_queue_t cq; // Contains read responses, for Ethernet tile -> host.
  char padding[4096 - sizeof(uint64_t)*16 - sizeof(eth_queue_t)*3];
  char buffers[4][1024]; // Used for CMD_DATA_BLOCK.
  char internal_buffers[20][1024];
} eth_base_firmware_queues_t;

typedef struct eth_queue_t {
  uint32_t wr_req_counter;
  uint32_t wr_resp_counter;
  uint32_t rd_req_counter;
  uint32_t rd_resp_counter;
  uint32_t error_counter;
  uint32_t padding0[3]; // Aligns next field to 16 bytes.
  uint32_t wr_idx;
  uint32_t padding1[3]; // Aligns next field to 16 bytes.
  uint32_t rd_idx;
  uint32_t padding2[3]; // Aligns next field to 16 bytes.
  routing_cmd_t entries[4];
} eth_queue_t;

typedef struct routing_cmd_t {
  uint64_t target_addr; // From low to high: 36-bit addr, 6 bits each NoC x/y, 6 bits each chip x/y (top 4 bits unused).
  union {
    uint32_t inline_data;       // When CMD_DATA_BLOCK is not set in flags.
    uint32_t data_block_length; // When CMD_DATA_BLOCK is set in flags.
  };
  uint32_t flags;
  uint16_t target_rack_xy; // From low to high: 8 bits each rack x/y.
  uint16_t reserved[5];
  uint32_t data_block_dram_addr; // Used for CMD_DATA_BLOCK_DRAM.
} routing_cmd_t;

// Values for the routing_cmd_t::flags bitmask:
#define CMD_WR_REQ           (1u <<  0)
#define CMD_RD_REQ           (1u <<  2)
#define CMD_RD_DATA          (1u <<  3)
#define CMD_DATA_BLOCK_DRAM  (1u <<  4)
#define CMD_DATA_BLOCK       (1u <<  6)
#define CMD_NOC_ID           (1u <<  9)
#define CMD_ORDERED          (1u << 12)
#define CMD_MOD              (1u << 13)
#define CMD_DEST_UNREACHABLE (1u << 31)
```

Each Ethernet tile's instance of `eth_base_firmware_queues_t` starts at address `0x11000` in its L1. Firmware will also publish the start address as a `uint32_t` at address `0x170` in L1.

Host software pushes on to the submission queue by:
1. Reading both of `sq.wr_idx` and `sq.rd_idx`, and spinning if the queue is full (i.e. `(wr_idx - rd_idx) & 7 >= 4`).
2. Populating `sq.entries[sq.wr_idx & 3]`, along with `buffers[sq.wr_idx & 3]` in the case of `CMD_WR_REQ | CMD_DATA_BLOCK`.
3. Advancing `sq.wr_idx` (i.e. `wr_idx = (wr_idx + 1) & 7`).

After pushing a read request, host software should subsequently pop a response from the completion queue by:
1. Reading both of `cq.wr_idx` and `cq.rd_idx`, and spinning if the queue is empty (i.e. `wr_idx == rd_idx`).
2. Reading `cq.entries[cq.rd_idx & 3].flags`, and spinning if the value is zero.
3. If `flags` did not indicate an error, consuming the data in `cq.entries[cq.rd_idx & 3].inline_data` or `buffers[cq.rd_idx & 3]`.
4. Advancing `cq.rd_idx` (i.e. `rd_idx = (rd_idx + 1) & 7`).

Note that the same `buffers` array is used for write requests and read responses. After pushing one or more read requests with `CMD_DATA_BLOCK` set, host software must pop all the corresponding read responses before pushing any write requests with `CMD_DATA_BLOCK` set.

## Request semantics

### `CMD_RD_REQ`

The host should populate `flags`, `target_addr`, and `target_rack_xy` in the request `routing_cmd_t`.

The service will populate the response `routing_cmd_t` fields:
* `flags` as `0` when the request is accepted, with this changing to a non-zero value once the request has been serviced: `CMD_RD_DATA` will be set, and additional flags will be set in case of error.
* `inline_data` as `0` when the request is accepted, with this changing to the read response data once the request has been serviced (assuming no errors were encountered).
* `target_addr` as a copy of the request's `target_addr`.
* `data_block_dram_addr` as `0`.

The service will increment `rd_req_counter` after the request is accepted, and increment `rd_resp_counter` after the request has been serviced. If the `CMD_DEST_UNREACHABLE` error was encountered during servicing, then `error_counter` will be incremented in addition to `rd_resp_counter`, and the `flags` field of the response will include `CMD_DEST_UNREACHABLE`.

### `CMD_RD_REQ | CMD_DATA_BLOCK`

The host should populate `flags`, `data_block_length`, `target_addr`, and `target_rack_xy` in the request `routing_cmd_t`.

The service will populate the response `routing_cmd_t` fields:
* `flags` as `0` when the request is accepted, with this changing to a non-zero value once the request has been serviced: `CMD_RD_DATA | CMD_DATA_BLOCK` will be set, and additional flags will be set in case of error.
* `data_block_length` as `0` when the request is accepted, with this changing to a copy of the request's `data_block_length` once the request has been serviced.
* `target_addr` as a copy of the request's `target_addr`.
* `data_block_dram_addr` as `0`.

Assuming no errors were encountered, the `data_block_length` bytes of read response data will be written to `buffers[i][0:data_block_length]`, where `i` is the index of the response `routing_cmd_t` within the completion queue.

The service will increment `rd_req_counter` after the request is accepted, and increment `rd_resp_counter` after the request has been serviced. If the `CMD_DEST_UNREACHABLE` error was encountered during servicing, then `error_counter` will be incremented in addition to `rd_resp_counter`, and the `flags` field of the response will include `CMD_DEST_UNREACHABLE`.

### `CMD_RD_REQ | CMD_DATA_BLOCK | CMD_DATA_BLOCK_DRAM`

The host should populate `flags`, `data_block_length`, `data_block_dram_addr`, `target_addr`, and `target_rack_xy` in the request `routing_cmd_t`.

The service will populate the response `routing_cmd_t` fields:
* `flags` as `0` when the request is accepted, with this changing to a non-zero value once the request has been serviced: `CMD_RD_DATA | CMD_DATA_BLOCK | CMD_DATA_BLOCK_DRAM` will be set, and additional flags will be set in case of error.
* `data_block_length` as `0` when the request is accepted, with this changing to a copy of the request's `data_block_length` once the request has been serviced.
* `target_addr` as a copy of the request's `target_addr`.
* `data_block_dram_addr` as a copy of the request's `data_block_dram_addr`.

Assuming no errors were encountered, the `data_block_length` bytes of read response data will be written to `data_block_dram_addr`. The service will wait to receive a write acknowledgement from the PCI Express tile prior to making `flags` non-zero. When `data_block_length > 1024`, multiple writes may be used; the service does not guarantee any particular ordering for the individual writes, but _does_ guarantee to wait for all write acknowledgements before making `flags` non-zero.

The service will increment `rd_req_counter` after the request is accepted, and increment `rd_resp_counter` after the request has been serviced. If the `CMD_DEST_UNREACHABLE` error was encountered during servicing, then `error_counter` will be incremented in addition to `rd_resp_counter`, and the `flags` field of the response will include `CMD_DEST_UNREACHABLE`.

### `CMD_WR_REQ`

The host should populate `flags`, `inline_data`, `target_addr`, and `target_rack_xy` in the request `routing_cmd_t`.

No response is provided.

The service will increment `wr_req_counter` after the request is accepted, and increment `wr_resp_counter` after the request has been serviced. If the `CMD_DEST_UNREACHABLE` error was encountered during servicing, then `error_counter` will be incremented in addition to `wr_resp_counter`.

### `CMD_WR_REQ | CMD_DATA_BLOCK`

The host should populate `flags`, `data_block_length`, `target_addr`, and `target_rack_xy` in the request `routing_cmd_t`. The host should write the data to be written to `buffers[i][0:data_block_length]`, where `i` is the index of the request `routing_cmd_t` within the submission queue.

If `CMD_MOD` is specified in `flags`, then the data in `buffers` is interpreted as a [scatter page](#scatter-pages), rather than plain data. In this case, the low 48 bits of `target_addr` do not need to be populated.

No response is provided.

The service will increment `wr_req_counter` after the request is accepted, and increment `wr_resp_counter` after the request has been serviced. If the `CMD_DEST_UNREACHABLE` error was encountered during servicing, then `error_counter` will be incremented in addition to `wr_resp_counter`.

### `CMD_WR_REQ | CMD_DATA_BLOCK | CMD_DATA_BLOCK_DRAM`

The host should populate `flags`, `data_block_length`, `data_block_dram_addr`, `target_addr`, and `target_rack_xy` in the request `routing_cmd_t`. The host should write the data to be written to `data_block_dram_addr`.

If `CMD_MOD` is specified in `flags`, then each 1024 bytes of data in `data_block_dram_addr` is interpreted as a [scatter page](#scatter-pages), rather than plain data. In this case, the low 48 bits of `target_addr` do not need to be populated.

No response is provided.

The service will increment `wr_req_counter` after the request is accepted, and increment `wr_resp_counter` after the request has been serviced. If the `CMD_DEST_UNREACHABLE` error was encountered during servicing, then `error_counter` will be incremented in addition to `wr_resp_counter`.

## Scatter pages

When `CMD_MOD` is specified as a modifier on `CMD_WR_REQ`, then:
* The low 48 bits of `target_addr` are ignored.
* Each 1024 bytes of data is interpreted as a scatter page. A scatter page consists of one or more scatter sections, concatenated together. There are three possible kinds of scatter section:
  * **Write with identical payload:** Specifies one or more writes, with all the writes having identical:
    * Data to write
    * High 4 bits of 36-bit address (but the low 32 bits can vary per write)
    * NoC X
    * NoC Y
  * **Write with identical length:** Specifies one or more writes, with all the writes having identical:
    * Length of data to write (but the actual data can vary per write)
    * High 4 bits of 36-bit address (but the low 32 bits can vary per write)
    * NoC X
    * NoC Y
  * **Padding:** Specifies that there are no more scatter sections within the scatter page. This is required when the earlier sections within a page do not occupy the full 1024 bytes of the page.

The first 12 bytes of a write section consist of:

|First&nbsp;bit|#&nbsp;Bits|Name|Purpose|
|--:|--:|---|---|
|0|4|`scatr_cmd`|Must be set to `1`|
|4|1|`payload_per_offset`|If `true`, the writes have identical length, but possibly varying payloads.<br/>If `false`, the writes have identical length and payload.|
|5|3|Reserved|Should be zero|
|8|8|`scatr_count`|The number of writes to perform|
|16|4|`start_addr_h`|Used as part of forming [`NOC_TARG_ADDR`](../NoC/MemoryMap.md#noc_targ_addr-and-noc_ret_addr) for the first write|
|20|6|`noc_x`|Used as part of forming [`NOC_TARG_ADDR`](../NoC/MemoryMap.md#noc_targ_addr-and-noc_ret_addr) for the first write|
|26|6|`noc_y`|Used as part of forming [`NOC_TARG_ADDR`](../NoC/MemoryMap.md#noc_targ_addr-and-noc_ret_addr) for the first write|
|32|8|`p_size`|The length of each write is `p_size * 4` bytes (cannot be zero)|
|40|8|`p_offset`|Used as part of forming the address of the payload for the first write (cannot be zero)|
|48|16|Reserved|Should be zero|
|64|32|`start_addr_l`|Used as part of forming [`NOC_TARG_ADDR`](../NoC/MemoryMap.md#noc_targ_addr-and-noc_ret_addr) for the first write|

The [`NOC_TARG_ADDR`](../NoC/MemoryMap.md#noc_targ_addr-and-noc_ret_addr) of the first write is formed as `start_addr_l | (start_addr_h << 32) | (noc_x << 36) | (noc_y << 42)`. The payload for the first write comes from the start address of the write section plus `p_offset * 4` bytes (so `p_offset` should be at least 3 to skip over the 12 bytes in the above table). If the two addresses are not congruent mod 32 (c.f. [NoC alignment requirements](../NoC/Alignment.md)), then the logic which interprets the write will use a temporary scratch buffer to realign the data by an appropriate multiple of 4 bytes.

If `scatr_count` is greater than 1, then multiple writes are performed. Addresses for subsequent writes are specified as _signed_ 32-bit offsets, with each offset relative to the address of the _first_ write. This array of offsets exists in the write section immediately after the fixed 12 bytes described in the above table; software is encouraged to set `p_offset` such that the payload data array is distinct from this offset array. If `payload_per_offset` is `true`, then `p_offset` is incremented by `p_size` after every write.

The total length of a write section is `(p_offset + p_size * (payload_per_offset ? scatr_count : 1)) * 4` bytes. Note that a write section is not permitted to span across multiple scatter pages.

A padding section consists of a single byte:

|First&nbsp;bit|#&nbsp;Bits|Name|Purpose|
|--:|--:|---|---|
|0|4|`scatr_cmd`|Must be set to `0xF`|
|4|4|Reserved|Should be zero|

The total length of a padding section is whatever length remains within the current scatter page. Note that scatter pages are always 1024 bytes; if `data_block_length` is not a multiple of 1024, then software _must_ include a padding section.

## Ordering

There are three distinct stages to each request:
1. Host software performing PCIe operations to push a request on to a submission queue.
2. Device software forwarding the request over zero or more NoC and/or Ethernet hops, until it reaches an Ethernet tile in the same ASIC as the target tile. Each such hop will push the request on to an internal queue in some other Ethernet tile, and then pop it from the original queue.
3. Device software performing the request by means of a NoC read or write (or a RISCV load or store, if the target tile is exactly the Ethernet tile that the previous stage ended at and `CMD_DATA_BLOCK` is not specified).

Ordering is described for each stage individually.

### Stage 1

Host software is responsible for ordering in stage 1; it must ensure that the write to advance the submission queue's `wr_idx` happens after all of the writes to populate the `routing_cmd_t` (and `buffers[i]`, if applicable), and ensure that reordering does not happen between multiple distinct writes to `wr_idx`. Device software will consume the submission queue in order, doing a mixture of:
1. For read requests, push an entry to the completion queue. These entries are allocated in order, with `flags` initially set to zero, but then _potentially_ filled in out of order.
2. For requests with `CMD_DATA_BLOCK_DRAM` set, if `data_block_length` is greater than 1024, internally split the request into multiple sub-requests, each with `data_block_length ≤ 1024`. The sub-requests will then be consumed in order.
3. For write requests with `CMD_DATA_BLOCK_DRAM` set, use a NoC read (against the PCI Express tile) to pull in the data.
4. Pass the request off to stage 2, or if already in the correct ASIC, skip stage 2 and go straight to stage 3.

### Stage 2

In stage 2, if the `CMD_ORDERED` flag is specified, then ordering between requests is maintained, provided that they target the same ASIC (as `CMD_ORDERED` causes the same sequence of Ethernet hops to be chosen, the chosen Ethernet hops statically determine the required NoC hops between Ethernet tiles, and queue semantics ensure ordering is maintained at each individual hop). If this flag is not specified, then requests can potentially take different routes to get to the target ASIC, which can lead to reordering. If different routes are taken, then requests can also _end_ stage 2 in different Ethernet tiles, and then potentially their execution in stage 3 can interleave.

### Stage 3

In stage 3, requests are processed in order. NoC writes are performed with the [`NOC_CMD_RESP_MARKED`](../NoC/MemoryMap.md#noc_ctrl) flag set, and at most one NoC request can be in flight at a time (†): once a NoC read or write has been initiated, device software will wait for the read response or write acknowledgement before initiating the next NoC read or write.

If performing a [scatter page](#scatter-pages) write, then the restriction on only having one NoC request in flight ensures strict ordering between the various writes: the scatter page will be interpreted in order, from start to finish, with a write acknowledgement received for each write before the next write is initiated.

> (†) As an exception to this, the combination of flags `CMD_WR_REQ | CMD_DATA_BLOCK | CMD_DATA_BLOCK_DRAM | CMD_ORDERED` is performed slightly differently. The stronger ordering in stage 2 allows for weaker ordering in stage 3: this combination of flags allows for multiple NoC writes to be in flight at once, though all the writes will relate to the same original host request (c.f. the splitting in stage 1), and device software will wait to receive all the write acknowledgements before issuing a NoC read or write relating to a different original host request.
