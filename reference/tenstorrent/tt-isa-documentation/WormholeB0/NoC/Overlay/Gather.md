# Gather from several overlay streams to one

Several streams within the same NoC Overlay coprocessor can be joined together to perform gather operations. When so configured, messages received on _any_ of the gather input streams will be internally transferred to the gather output stream (without having to copy the message around in L1), and then transmitted by the gather output stream. Any stream can act as a gather input stream (transmit to gatherer), but only certain streams can act as a gather output stream (receive in gather mode); see [stream capabilities](README.md#stream-capabilities) for details. Additionally, a stream cannot simultaneously receive in gather mode and transmit to a gatherer.

Gather output streams have a concept of _groups_, where a group can be:
* A single gather input stream.
* A pair of two gather input streams, having stream IDs `i` and `i + 1`, where `i % 2 == 0`.
* A quartet of four gather input streams, having stream IDs `i` through `i + 3`, where `i % 4 == 0`.

A gather output stream can gather from any number of groups, albeit all groups must be the same size, and a gather input stream cannot be a member of more than one group. A gather output stream is continuously choosing a group to receive messages from (which can either be strict in-order iteration of groups by ascending stream ID, or round-robin arbitration of whatever groups have messages available) and then receiving a fixed number of messages from every stream in that group (which is always strict in-order iteration of streams within the group by ascending stream ID).

A gather output stream does not need a message header array in L1, nor a receiver buffer FIFO in L1. Instead, its message metadata FIFO can remember which gather input stream each message came from, and this is used to obtain the relevant L1 pointers from the gather input stream. This extra memory comes at a cost: when acting as a gather output stream, the message metadata FIFO of that stream has a maximum capacity of just two messages, even if its maximum capacity would normally be higher than that. Message metadata gets moved from the message metadata FIFOs of gather input streams to the message metadata FIFO of the gather output stream. The gather output stream then transmits as normal, and when it later pops from its L1 read complete FIFO, the corresponding gather input stream has its receive buffer FIFO read pointer advanced.

## Configuration

To configure the phase at the receiver (the gather output stream), software should:
1. Set `LOCAL_SOURCES_CONNECTED` within `STREAM_MISC_CFG_REG_INDEX` (and clear both `REMOTE_SOURCE` and `SOURCE_ENDPOINT`).
2. Set [`STREAM_GATHER_REG_INDEX`](#stream_gather_reg_index) to configure the group size and whether groups are iterated in-order or round-robin arbitration of whatever groups have messages available.
3. [`STREAM_GATHER_CLEAR_REG_INDEX`](#stream_gather_clear_reg_index) to configure how many messages to receive from each stream within a group every time a group has been selected.
4. Set `STREAM_LOCAL_SRC_MASK_REG_INDEX` and `STREAM_LOCAL_SRC_MASK_REG_INDEX + 1` and `STREAM_LOCAL_SRC_MASK_REG_INDEX + 2` with a bitmask of which stream IDs are being gathered from. Each of these registers contains 24 bits of mask in the low bits, with the high 8 bits unused: `STREAM_LOCAL_SRC_MASK_REG_INDEX` is used for streams 0 through 23, `STREAM_LOCAL_SRC_MASK_REG_INDEX + 1` for 24 through 47, and `STREAM_LOCAL_SRC_MASK_REG_INDEX + 2` for 48 through 63. If the bitmask includes _any_ stream in a group, it must include _all_ the streams in that group.

To configure the phase at each transmitter (the gather input streams), software should:
1. Set `LOCAL_RECEIVER` within `STREAM_MISC_CFG_REG_INDEX` (and clear both `REMOTE_RECEIVER` and `RECEIVER_ENDPOINT`).
2. Set [`STREAM_LOCAL_DEST_REG_INDEX`](#stream_local_dest_reg_index) with the stream ID of the receiver and how many messages the gather input stream needs to have available before the gather output stream will consider receiving from it.

## Handshake

Before _any_ messages are transferred from a gather input stream to the gather output stream, _all_ of the gather input streams need to have started the phase. A lightweight handshake is performed between the gather output stream and all gather input streams to ensure this.

## Register reference

### `STREAM_GATHER_REG_INDEX`

This register is present in the receiving stream.

|First&nbsp;bit|#&nbsp;Bits|Name|Purpose|
|--:|--:|---|---|
|0|3|`MSG_ARB_GROUP_SIZE`|The number of streams in a group; the allowed values are `1` or `2` or `4`|
|12|1|`MSG_SRC_IN_ORDER_FWD`|If `true`, the receiver will iterate through groups in strictly ascending order of stream ID. The receiver will not skip over any groups, and if the next group in order isn't yet ready, the receiver will wait for it to become ready. If `false`, the receiver will perform round-robin arbitration between groups that are ready. In either case, a group is ready once all streams within the group are offering to transmit messages to the receiver.|
|13|19|Reserved|Writes ignored, reads as zero|

The functional model for how the receiver iterates over groups is roughly:

```c
while (MoreMessagesToReceiveThisPhase) {
  if (MSG_SRC_IN_ORDER_FWD) {
    for (unsigned g = 0; g < 64; g += MSG_ARB_GROUP_SIZE) {
      if (Combined_STREAM_LOCAL_SRC_MASK_REG_INDEX.Bit[g]) {
        while (!IsGroupReady(g)) {
          wait;
        }
        ReceiveFromGroup(g);
      }
    }
  } else {
    for (unsigned g = 0; g < 64; g += MSG_ARB_GROUP_SIZE) {
      if (Combined_STREAM_LOCAL_SRC_MASK_REG_INDEX.Bit[g] && IsGroupReady(g)) {
        ReceiveFromGroup(g);
      }
    }
  }
}
```

With supporting definition:

```c
bool IsGroupReady(unsigned g) {
  for (unsigned i = 0; i < MSG_ARB_GROUP_SIZE; ++i) {
    if (!IsStreamReady(g + i)) {
      return false;
    }
  }
  return true;
}
```

### `STREAM_GATHER_CLEAR_REG_INDEX`

This register is present in the receiving stream.

|First&nbsp;bit|#&nbsp;Bits|Name|Purpose|
|--:|--:|---|---|
|0|16|`MSG_LOCAL_STREAM_CLEAR_NUM`|After selecting a group to receive from, the number of messages to receive from each stream within the group before choosing a new group to receive from.|
|16|1|`MSG_GROUP_STREAM_CLEAR_TYPE`|Controls the loop order within `ReceiveFromGroup`, which has an effect if both of `MSG_ARB_GROUP_SIZE` and `MSG_LOCAL_STREAM_CLEAR_NUM` are greater than one.|
|17|15|Reserved|Writes ignored, reads as zero|

The functional model of `ReceiveFromGroup` is then:

```c
void ReceiveFromGroup(unsigned g) {
  if (MSG_GROUP_STREAM_CLEAR_TYPE == 1) {
    for (unsigned i = 0; i < MSG_ARB_GROUP_SIZE; ++i) {
      for (unsigned j = 0; j < MSG_LOCAL_STREAM_CLEAR_NUM; ++j) {
        ReceiveFromStream(g + i);
      }
    }
  } else {
    for (unsigned j = 0; j < MSG_LOCAL_STREAM_CLEAR_NUM; ++j) {
      for (unsigned i = 0; i < MSG_ARB_GROUP_SIZE; ++i) {
        ReceiveFromStream(g + i);
      }
    }
  }
}
```

Where `ReceiveFromStream` waits for the given stream number to have a message in its message metadata FIFO, waits for the gather output stream to have space in its message metadata FIFO, and then transfers the metadata from the given stream number to the gather output stream. If the message metadata FIFO in the gather output stream includes a full copy of the message header, the gather output stream will load this from the given stream number's message header array.

### `STREAM_LOCAL_DEST_REG_INDEX`

This register is present in the transmitting stream.

|First&nbsp;bit|#&nbsp;Bits|Name|Purpose|
|--:|--:|---|---|
|0|12|`STREAM_LOCAL_DEST_MSG_CLEAR_NUM`|The number of messages that need to be available for transmission (i.e. have been received but not yet transmitted) before the stream will return true for `IsStreamReady`. Software is likely to want either `1` or `MSG_LOCAL_STREAM_CLEAR_NUM`, but any value is permitted.|
|12|6|`STREAM_LOCAL_DEST_STREAM_ID`|The stream ID of the receiver|
|18|14|Reserved|Writes ignored, reads as zero|
