# PixelWorld Thread Scheduler

`pixelworld_thread` is an optional L1 adapter between the L0 PixelWorld candidate
scheduler contract and the engine JobSystem. `JobSystemPixelScheduler` borrows a
running JobSystem, submits one synchronous fork/join range, and returns only after
all workers have finished.

The current scheduled backend parallelizes thermal contribution generation. Each
worker reads the same immutable post-movement phase and writes only one Chunk-owned
buffer. PixelWorld then merges buffers in canonical Chunk/coordinate order. A worker
failure triggers a serial overwrite of every result slot, preserving reference-backend
semantics without exposing partial candidates.
