# PixelWorld 兴趣区 Streaming

`pixelworld_streaming` 是可裁剪的 L1 投影模块。`PixelChunkStreamCursor` 按 Chunk 坐标
兴趣区捕获新进入、发生 revision 变化和离开兴趣区的 Chunk；离开区域使用 tombstone
通知消费方释放投影。Cursor 只拥有 source link、上次 revision 与已知坐标，不保留
`PixelWorld` 指针，也不成为材料状态的第二权威来源。

每个 `PixelChunkBatch` 携带材料 Catalog fingerprint、世界 seed、权威 revision、
simulation Tick 和最后 edit sequence。`PixelWorld::applyChunkBatch()` 使用
`expectedRevision` 拒绝 stale writer，先完整验证唯一 canonical y/x 顺序、固定 64×64
cell 数量、材料 ID 和单调 metadata，再一次性提交全部替换与 tombstone。失败不修改世界；
成功会推进 world epoch，使旧结构碎片 link 失效。

兴趣区上限为 65536 个 Chunk 坐标。source epoch 改变会强制 cursor 全量重同步。
该批次是 transport-neutral 的 owning 数据，可用于增量存档或网络消息。source epoch
变化产生的 `fullResync` 批次会原子替换消费方投影，因此允许权威 revision/Tick 回退，
同时清除旧 epoch 中未显式列出的 Chunk；普通增量批次仍拒绝 metadata 回退。

`ReliablePixelChunkSender`/`ReliablePixelChunkReceiver` 在字节传输之上提供应用层可靠会话：
每个 transfer 默认按单 Chunk 分片，发送端采用有界 in-flight 窗口并保留 owning payload
和 tombstone，直到收到证明整批已提交的累计 ACK；接收端有界缓存乱序 part，只在下一
连续 transfer 完整、metadata 一致且 `applyChunkBatch()` 成功后推进 ACK。重复 part/ACK
幂等，冲突重复包和越过发送窗口的 ACK 会被拒绝，窗口或分片预算失败也不会推进 capture
cursor。调用方可把 part 与 ACK 编码后放入 `UdpLink::Reliable`/可靠 RPC；UDP 的包级 ACK
不能替代这里的应用提交 ACK。拥塞控制、身份认证与会话加密仍属于宿主网络层。

`archiveChunkBatch()` 使用公共 `SnapshotEnvelope`，type 为 `pixelworld.chunk-batch`、
schema 为 `pixelworld:chunk-batch`，当前 version 3。每个 present Chunk 独立编码为固定
little-endian cell bytes，可选择 `None` 或 zstd level 3，并保存独立 decoded-content hash；
外层 envelope hash 同时认证 metadata、目录和压缩 payload。`decodeChunkBatchArchive()`
先验证外层，再按 512 MiB 总解码预算逐 Chunk 解压和验 hash，只返回 owning candidate，
不会写世界。version 3 同时持久化 `fullResync`，cell payload 包含热能 residual；
version 1/2 的 5-byte legacy cell
仍可迁移，缺失 residual 恢复为 0，version 1 缺失的 `sourceLastEditSequence` 也恢复为 0。

源码：[`src/modules/pixelworld_streaming/`](../../../src/modules/pixelworld_streaming/)。
测试：[`test/pixelworld_streaming.cpp`](../../../test/pixelworld_streaming.cpp)。
