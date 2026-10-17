# Overlay streams transmitting to nowhere

An overlay stream can be configured to transmit messages to nowhere. If so configured, received messages will be written to the receive buffer FIFO, and their headers written to the message header array, but they will not be transmitted anywhere. Message metadata _will_ be pushed into the message metadata FIFO, but then immediately popped. This will cause an entry to be pushed onto the L1 read complete FIFO, but that too will be immediately popped.

To configure the phase, software should:
1. Clear all of `RECEIVER_ENDPOINT`, `LOCAL_RECEIVER`, and `REMOTE_RECEIVER` within `STREAM_MISC_CFG_REG_INDEX`.
