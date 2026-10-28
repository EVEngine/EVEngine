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

## 接到游戏的模拟 owner

本模块是原生 JobSystem adapter，没有独立 EveScript 构造/调度入口。
游戏继续使用 PixelWorld 的正式 step 路径；不能在 worker 中调用脚本或 Graphics。
原生宿主先启动 JobSystem，再设置候选 scheduler，在主模拟线程等待 fork/join 完成。

任务一是批量热传导：所有 worker 读取同一不可变阶段，完成后以固定顺序合并。
任务二是失败恢复：注入 worker 错误，确认 serial overwrite 覆盖所有候选槽，
并与串行参考后端逐 tick 比较状态。退出先停 PixelWorld 调度，再释放借用的 JobSystem。

对游戏开发者，先建立确定性串行场景再开启调度；速度提升需固定 chunk 数、材料与 tick
测量，不能以线程数推断收益。
