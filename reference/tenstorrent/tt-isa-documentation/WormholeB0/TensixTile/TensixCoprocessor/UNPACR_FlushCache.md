# `UNPACR` (Flush unpacker decompression row start cache)

**Summary:** Flush the cache which unpacker input address generators use for determining the byte-level start/end points within compressed data. Software might need to use this between [`UNPACR` (Move datums from L1 to `SrcA` or `SrcB` or `Dst`)](UNPACR_Regular.md) instructions if performing decompression as part of unpacking, and it is rapidly reusing the memory containing compressed data.

**Backend execution unit:** [Unpackers](Unpackers/README.md)

## Syntax

```c
TT_OP_UNPACR(/* u1 */ WhichUnpacker,
             0,
             false,
             0,
             0,
             /* bool */ MultiContextMode,
             false,
             false,
             false,
             false,
             false,
             true,
             false)
```

## Encoding

![](../../../Diagrams/Out/Bits32_UNPACR_FlushCache.svg)

## Functional model

The cache is not modelled, so no functional model is provided. The physical manifestation of Wormhole has four one-entry caches per unpacker. When `MultiContextMode` is `true`, all four are cleared. Otherwise, just the cache for the current thread is cleared.
