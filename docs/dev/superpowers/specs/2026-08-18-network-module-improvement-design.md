# 网络模块改进设计

日期：2026-08-18
状态：阶段 1 部分完成（1.1、1.2 已实现并验证；1.3、1.4 依赖阻塞）

## 背景

`network` 模块目前是「传输原语 + 简单消息帧」层：

- `TcpSocket` / `UdpSocket`：Poco 非阻塞 socket，`NetWorker` 单线程每 5ms 轮询。
- `Channel`：TCP 之上的 4 字节长度前缀帧（大端），单路消息流。
- `Session`：名字 → `Channel` 的映射表，无 peer ID / 心跳 / 生命周期事件。
- `HttpRequest`：HTTP 客户端（`https://` 直接返回 `Err("tls")`，Poco NetSSL 未链接）。

对照 Unity（Netcode for GameObjects + Unity Transport + Unity Gaming Services）与
Godot 4.x 内置网络栈，差距集中在「游戏网络层」：UDP 可靠性、类型化序列化、RPC、
连接生命周期、状态复制、实体同步、权威模型，以及加密、模拟器、统计与外部服务接入。
完整差距矩阵见 `docs/dev/游戏开发系统与工具差距分析.md` 与
`evengine-network-gap-analysis.canvas.tsx`（会话可视化面板）。

## 目标

按五个阶段补齐，每阶段可独立落地并验证：

1. **传输与编码**：HTTPS/TLS、TCP 部分发送修复、UDP 可靠层（ENet 或自研 ARQ）、
   `NetReader`/`NetWriter` 类型化序列化。
2. **RPC 与会话**：消息注册表、定向/广播发送、发送者 ID、peer ID/心跳/超时/生命周期事件。
3. **复制与权威**：脏标记状态复制（NetworkVariable 等价物）、spawn/despawn 同步、所有权模型。
4. **工具与平台**：网络模拟器（延迟/丢包/抖动）、带宽/RTT 统计与 profiler、WebSocket、局域网发现。
5. **规模化服务**：Relay/NAT、大厅/匹配、语音（外部服务接入层）。

本文档详细设计阶段 1，其余阶段在「后续阶段」一节给出设计要点，随实现推进补充。

## 非目标

- 阶段 1 不做 RPC / 状态复制（阶段 2 / 3）。
- 不引入外部服务（Relay / Lobby / Vivox 均为阶段 5 的接入层设计）。
- 不重写 `NetWorker` 为 epoll/IOCP 事件驱动（阶段 4 评估，5ms 轮询先维持）。
- 不改变现有 `Channel` 帧格式的兼容性（4 字节长度前缀保留，大端序不迁移）。

## 方案：阶段 1 详细设计

### 1.1 TCP 部分发送修复（本阶段落地）

**现状问题**：`TcpSocket::send` 在 worker 线程对非阻塞 socket 只调一次 `sendBytes`，
忽略返回值；大帧可能只发出前半段却被记为已发送（`sendQueued_` 全额递减）。

**方案**：

- `TcpSocket` 新增挂起发送缓冲 `pendingSend_`（`std::vector<char>` + `std::mutex`）。
- `send()` 只做「拷贝入队 + 限额检查」（仍以 `kMaxSendBuffer` 为上限，超限回
  `Err("limit")`），不再提交一次性 worker job。
- `Network::pollSockets` 在 worker 线程为每个已连接 TCP socket 调用
  `flushSend()`：循环 `sendBytes` 直到发完；`Poco::TimeoutException`（would-block）
  视为未发完、留待下轮；其余异常按断线处理并回 `Err("closed")`。
- `close()` / 析构清空挂起缓冲；`pendingSendBytes()` 保留作统计/调试用。

### 1.2 NetReader / NetWriter（本阶段落地）

目标：替代脚本手写字节封包，提供类型化、越界安全的读写原语。

- 纯 C++20，不依赖 Poco，放在 `src/modules/network/`，可独立编译测试。
- 字节序约定：**小端**（主流桌面/移动端一致），定长类型原样写入。
- `NetWriter`：`writeU8/I8/U16/I16/U32/I32/U64/I64/F32/F64/Bool` +
  `writeString`（uint32 长度前缀 + 字节）、`writeBytes`；`size()` / `data()` /
  `toString()` 取结果。
- `NetReader`：对应 `u8/i8/u16/i16/u32/i32/u64/i64/f32/f64/bool/str/bytes(n)`；
  越界读取置粘性错误位（`ok() == false`），后续读取安全返回默认值；`remaining()` /
  `pos()` 便于调试。
- Squirrel 绑定：`eve.Network().newWriter()` / `newReader(bytes)`，类同时可脚本
  直接构造（`NetWriter()` / `NetReader().init(bytes)`），GC 托管释放。
  脚本侧组合：`channel.sendMsgString(writer.toString())`。
- 消息长度沿用 `kMaxFrameSize`（1MB）约束。

### 1.3 HTTPS / TLS（依赖阻塞，设计先行）

**现状问题**：`HttpRequest::submit()` 对 `https://` 直接返回 `Err("tls")`；
预编译 third-party 未包含 Poco NetSSL_OpenSSL 与 OpenSSL。

**方案**：

- CMake 检测 `PocoNetSSL` + OpenSSL：可用时定义 `EVE_HAVE_NETSSL` 并链接，
  不可用时保持现有「tls 错误」路径（行为不回归）。
- `HttpRequest` 在 `EVE_HAVE_NETSSL` 下改用 `Poco::Net::HTTPSClientSession` +
  `SSLManager`/`Context`；`verifySsl` 映射到 `SSLManager::VERIFY_PEER` /
  `VERIFY_NONE`。
- 依赖树需要：third-party 聚合仓库新增 OpenSSL 并启用 Poco `NetSSL_OpenSSL`
  组件，重编预编译树。**本机无网络/无聚合仓库写权限，此步骤待依赖更新后落地。**

### 1.4 UDP 可靠层 / ENet（依赖阻塞，设计先行）

**现状问题**：UDP 只有裸 `sendTo`，无可靠性、顺序、分片、心跳。

**方案选项**：

- A. 集成 ENet：需 third-party 聚合仓库新增 enet 源码并重编预编译树（依赖阻塞）。
- B. 自研轻量 ARQ（约 600-900 行，可完全在本仓库实现）：
  `NetChannel` over UDP，提供 可靠 / 不可靠 / 不可靠有序 / 分片 + seq/ack/重传/
  乱序缓冲/心跳，多路复用通道号。

**决策**：传输后端抽象一个 `MultiPeer` 接口（connect/listen/poll/send 通道化），
阶段 1 先按 B 实现 `UdpReliableChannel`；若后续 third-party 提供 ENet，则新增
`EnetPeer` 实现同一接口，上层（阶段 2 RPC）不感知切换。

## 后续阶段设计要点

### 阶段 2 · RPC 与会话

- `NetMessage` 头：msgId + 目标（server/all/peer/group）+ 可靠性 + 通道号 + senderId。
- C++ 注册表 `NetDispatcher`：msgId → `std::function<void(NetReader&, sender)>`，
  脚本侧映射到 Squirrel 闭包（经 `eve.Event` 或直接回调）。
- `NetSession` 升级：peer ID 分配、maxClients、心跳/超时、`peer_connected` /
  `peer_disconnected` / `connected_to_server` / `connection_failed` 事件。

### 阶段 3 · 复制与权威

- `NetVar<T>`：脏标记 + 每 tick 同步 + 晚加入快照 + 权限（谁可写）。
- `NetObject`：spawn/despawn 同步、对象 ID 表、owner（`setMultiplayerAuthority`）。
- `NetTransform`：位置/旋转同步 + 插值缓冲。

### 阶段 4 · 工具与平台

- 网络模拟器：延迟/丢包/抖动注入（发送队列处）。
- 统计：每 peer 带宽、包计数、RTT；暴露给脚本与编辑器面板。
- `WebSocketPeer`（Poco WebSocket 或自研）、UDP 广播局域网发现。

### 阶段 5 · 规模化服务

- Relay/NAT 穿透、大厅/匹配、语音：均为外部服务，引擎只保留接入层接口
  （`NetService` 抽象），不在引擎内实现服务器。

## 文件清单

| 文件 | 内容 |
|---|---|
| `docs/dev/superpowers/specs/2026-08-18-network-module-improvement-design.md` | 本文档 |
| `src/modules/network/NetStream.h` / `NetStream.cpp` | NetReader / NetWriter（阶段 1.2） |
| `src/modules/network/TcpSocket.h` / `TcpSocket.cpp` | 挂起发送缓冲 + 部分发送修复（阶段 1.1） |
| `src/modules/network/Network.cpp` | `pollSockets` flush + `newWriter` / `newReader` 绑定 |
| `src/modules/network/HttpRequest.cpp` | TLS 分支（`EVE_HAVE_NETSSL`，待依赖） |
| `CMakeLists.txt` | NetSSL/OpenSSL 检测（待依赖） |
| `test/network.cpp` | 部分发送、序列化用例 |
| `docs/usr/modules/network.md` | API 快查同步 |

## 验证

- 配置：`cmake -S . -B build/win32-debug -G Ninja
  -DEVENGINE_THIRD_PARTY_BINARY_DIR=<预编译树>`（本机指向主仓库
  `build/third-party-binary/win32-debug`）。
- 编译：`cmake --build build/win32-debug --target unit_test`。
- 网络用例：`unit_test --testcase="network.*"` 全绿。
- 全量套件无新增失败。

## 阶段 1 落地记录（2026-08-18）

已实现：

- **1.1 TCP 部分发送修复**：`TcpSocket` 增加挂起发送队列 `pendingSend_`（互斥保护），
  `send()` 入队并做 1MiB 限额检查；`Network::pollSockets` 每轮调用 `flushSend()`。
  Windows 下 Poco 非阻塞缓冲满会抛 `IOException("Operation would block")`（code 为
  `WSAEWOULDBLOCK`），已按 would-block 处理（保留余量下轮再发），不再误判断线。
- **1.2 NetReader / NetWriter**：`NetStream.h/cpp` 提供小端类型化读写
  （定长整数/浮点/bool/长度前缀 string/bytes），越界读取粘性置错（`ok()`）。
  Squirrel 绑定：`eve.Network().newWriter()` / `newReader(bytes)`，类可脚本直接构造；
  写端 `toString()` 可直接配 `sendMsgString()`。
- 附带修复：`TcpSocket::connect` 超时参数单位错误
  （`Poco::Timespan(ms, 0)` 原本把毫秒当秒，2.7 小时超时），改为
  `Timespan(ms/1000, (ms%1000)*1000)`。

验证：

- 独立验证程序（`build/win32-debug/net_standalone_test.cpp`，绕过引擎事件栈，
  仅链接 network 模块 + Poco + 脚本库）四项全绿：
  序列化往返 / 越界安全 / 2 MiB TCP 冲刷（逐字节校验）/ 512 KiB Channel 帧重组。
- `test/network.cpp` 新增同套用例，单独编译通过；全量 `unit_test` 本机受阻于
  **既有** `src/modules/sceneloader/SceneLoader.cpp` 编译错误（引用 `SceneNode`
  不存在的成员，主仓库该文件已更新、本 worktree 落后），与本改动无关。

待依赖更新后继续：1.3 HTTPS/TLS（third-party 需补 OpenSSL + Poco NetSSL_OpenSSL）、
1.4 UDP 可靠层（自研 ARQ 或 ENet）。

## 1.4 与阶段 2 设计（2026-08-19 追加）

### 1.4 UDP 可靠层：`UdpLink`

点对点 UDP 链路（与 `Channel`/`TcpSocket` 的配合方式一致：`UdpLink` 持有外部
`UdpSocket`），全部状态只在主线程（`Network::pump`）推进，socket 收发仍走
`NetWorker`。支持三种消息语义：

- `Reliable`：序号 + ACK + 超时重传（100ms 起指数退避，上限 10 次），按序投递。
- `Unreliable`：尽力而为，无序。
- `UnreliableOrdered`：带序号，接收端丢弃迟到的乱序包（Godot 同语义）。

数据报格式（小端，`NetWriter` 风格手工组装）：

```
[0]  u8  magic0 'E'        [1]  u8 magic1 'V'      [2] u8 version=1
[3]  u8  type              [4]  u8 channel          [5] u8 flags (bit0=分片)
[6]  u32 seq               [10] u32 ack             [14] u32 ackBits
[18] （可选，flags&1）u32 msgId + u16 fragCount + u16 fragIndex（8 字节）
[18/26] payload
```

type：0=ReliableData 1=UnreliableData 2=UnreliableOrderedData 3=Ack 4=Ping 5=Pong。
`ack` = 接收端最后连续序号，`ackBits` bit i 表示 `ack+1+i` 已收到（乱序缓冲）。

接收端：连续序号直接投递并推进 `expectedReliable`，乱序包进有界缓冲（≤2048），
每收到数据包立即回裸 Ack（附带乱序位图）。分片：载荷 >1200B 按消息拆分
（`msgId + fragCount + fragIndex`），重组缓冲 5s 过期。心跳：空闲 500ms 发 Ping，
对端回 Pong；超过 `timeoutMs`（默认 10s）未收到任何包判死并回调。

测试钩子：`setLossRate(0..1)` 随机丢出站包，用于验证重传与重组。

### 阶段 2 · RPC 与会话

- **`NetRpc`**（可挂在任意 `UdpLink`/`TcpSocket` 上）：消息信封
  `u16 msgId + payload`；C++ 侧注册 `std::function<void(NetReader&)>`，脚本侧注册
  闭包（收到 `bytes` 字符串参数，脚本自行 `newReader(bytes)`）。
- **`NetHost`**：服务端共享一个绑定 socket，按 `sender.toString()` 地址路由到
  对应 `UdpLink`；首个数据报自动建链并分配递增 peerId；链死触发
  `peer_disconnected`。客户端 `connectTo` 建链，收到首个 Pong/数据视为已连接。
- 事件：`peerconn`(peerId)、`peerdisconn`(peerId, reason)；RPC 消息经脚本闭包派发。
- 权威/所有权、状态复制、spawn/despawn 仍归阶段 3；本阶段只补「能可靠地远程调用 +
 管理连接生命周期」。

### 阶段 1.4 / 阶段 2 落地记录（2026-08-19）

已实现并验证：

- **`UdpLink`**（`src/modules/network/UdpLink.h/cpp`）：点对点 UDP 链路，三种语义
  （Reliable / Unreliable / UnreliableOrdered）+ 分片重组 + ACK/重传 + 心跳 + 丢包
  模拟。`Network::pump` 负责路由入站数据报并驱动重传/心跳/超时（全主线程）。
- **`NetRpc`**（`NetRpc.h/cpp`）：`u16 msgId + payload` 信封，C++ 处理器收
  `NetReader&`，脚本处理器收 payload 字符串（可 `newReader(bytes)` 自建）。
- **`NetHost`**（`NetHost.h/cpp`）：共享一个绑定 socket 的多 peer 路由，按发送方
  地址自动建链并分配递增 peerId，链死触发 `peer_disconnected`（事件 + C++ 回调）。
- 平台适配：UDP socket 接收/发送缓冲提到 1MiB（Windows 默认 8KB 会丢突发分片）；
  worker 每轮最多排空 64 个 UDP 数据报。
- 关键修复（调试中发现）：序号采用 u32 环形语义，`ack = expected-1` 在未收到任何
  数据时为 `0xFFFFFFFF`，发送端 `pruneAcked` 必须用环形比较，否则会把所有待确认
  消息误删；`ackSend_` 初始值同样为 `0xFFFFFFFF`。

验证（`build/win32-debug/net_standalone_test.cpp`，独立链接 network 模块）：

- UDP 可靠：40 条消息在双向 25% 丢包下全部按序到达、无重复、队列清零。
- UDP 分片：48KiB 消息在 10% 丢包下完整重组（41 个分片）。
- UDP 有序：12 条 UnreliableOrdered 全部按序到达。
- RPC：双向 `call`/`registerHandler` 往返正确。
- NetHost：客户端建链（peerId=1）→ 双向消息 → 客户端失联（丢包率 1.0）后
  `peer_disconnected` 触发、peer 移除。
- `test/network.cpp` 同步新增同名用例，单独编译通过。

阶段 3（复制与权威）设计要点已在「后续阶段设计要点」中，随实现补充。
