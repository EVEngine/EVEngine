# 网络模块

**脚本入口：** `eve.Network()`

提供 HTTP 请求、TCP 客户端/服务端、UDP、UDP 可靠链路（UdpLink）、RPC（NetRpc）、
多 peer 会话（NetHost）、类型化序列化（NetWriter/NetReader）和基础网络状态。

改进设计见 [`docs/dev/superpowers/specs/2026-08-18-network-module-improvement-design.md`](../../dev/superpowers/specs/2026-08-18-network-module-improvement-design.md)。

## 基本用法

```squirrel
local net = eve.Network();
local req = net.newHttp("GET", "https://example.com/api");
req.setTimeout(5000);
req.submit();
// 在主循环调用 net.pump() 派发完成事件。
```

## 对象关系与调用时机

`Network` 创建 TcpSocket/UdpSocket/HttpRequest/Channel/Session，并通过 `pump()` 推进异步完成。Socket 负责传输，Channel 负责消息封装，Session 负责连接集合。

## 目标导向指南

### 发起异步 HTTP 请求

用 `newHttp(method, url)`，设置 header、body、timeout 和 TLS 校验后 `submit()`；每帧调用 `net.pump()` 派发结果。不要阻塞渲染线程等待网络。

### 建立 TCP 会话

服务端 `newTcp()` 后 bind/listen/accept，客户端 connect；用 `newChannel(socket)` 封装消息发送，并把多个连接加入 `newSession()` 统一管理。断线时从 Session 移除并清理 socket。

### 封包并发送类型化消息

用 `newWriter()` 写入字段，`toString()` 取字节串后经 `sendMsgString()` 发送；接收端用 `newReader(bytes)` 或 `reader.init(bytes)` 读取：

```squirrel
local w = net.newWriter();
w.writeU32(42);
w.writeString("hello");
channel.sendMsgString(w.toString());
// 对端：
local r = net.newReader(receivedBytes);
local n = r.u32();
local s = r.str();
```

读取越界后 `reader.ok()` 变为 false，后续读取安全返回默认值（空/0）。

### UDP 可靠链路（UdpLink）

```squirrel
local udp = net.newUdp();
udp.connect("127.0.0.1", 7000);
local link = net.newUdpLink(udp);
link.setRemote("127.0.0.1", 7000);
link.onMessage(function(channel, bytes) {
    local r = net.newReader(bytes);
    print(r.u32());
});
link.sendReliable(0, payloadString);      // 可靠、按序
link.sendUnreliable(1, payloadString);    // 尽力而为
link.sendOrdered(2, payloadString);       // 带序但不可靠
```

### RPC（NetRpc）

```squirrel
local rpc = net.newRpc(link);
rpc.registerRpc(7, function(bytes) {
    local r = net.newReader(bytes);
    print("got rpc 7: " + r.str());
});
local w = net.newWriter();
w.writeString("hello");
rpc.callRpc(7, w.toString(), true);   // 可靠调用
```

### 多 peer 会话（NetHost）

```squirrel
local host = net.newHost();
host.start(7000);
host.onMessage(function(peerId, channel, bytes) {
    // 首个数据报自动建链并分配 peerId
    host.sendReliable(peerId, 0, "pong");
});
host.onPeerConnected(function(peerId) { /* peerconn */ });
host.onPeerDisconnected(function(peerId) { /* peerdisconn */ });
```

## 常见问题

- 提交 HTTP 后不 pump：请求完成不会被主线程处理。
- 发布环境关闭 TLS 校验：`setVerifySsl(false)` 只能用于受控调试。
- 假设 TCP 一次 send 对应一次 receive：应用层必须定义消息边界。
- 发送数据不立即写出：`send()` 先入队，由网络线程轮询冲刷；大流量下 `pendingSendBytes()` 可见积压。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `accept()`、`add()`、`bind()`、`bool()`、`bytes()`、`callRpc()`、`close()`、`closeAll()`、`connect()`、`f32()`、`f64()`
- `get()`、`getName()`、`getPeer()`、`getSocket()`、`i8()`、`i16()`、`i32()`、`i64()`、`init()`、`initString()`、`isAlive()`
- `isConnected()`、`link()`、`listen()`、`newChannel()`、`newHost()`、`newHttp()`、`newReader()`、`newRpc()`、`newSession()`
- `newTcp()`、`newUdp()`、`newUdpLink()`、`newWriter()`、`ok()`、`onMessage()`、`onPeerConnected()`、`onPeerDisconnected()`
- `peer()`、`peerCount()`、`peerId()`、`pendingFragments()`、`pendingReliable()`、`pos()`、`pump()`、`registerRpc()`、`remaining()`
- `remove()`、`send()`、`sendMsg()`、`sendMsgString()`、`sendOrdered()`、`sendReliable()`、`sendString()`、`sendTo()`、`sendToString()`
- `sendUnreliable()`、`setBody()`、`setBodyString()`、`setHeader()`、`setLossRate()`、`setRemote()`、`setRemoteString()`、`setTimeout()`
- `setTimeoutMs()`、`setVerifySsl()`、`size()`、`start()`、`str()`、`submit()`、`toString()`、`u8()`、`u16()`、`u32()`、`u64()`
- `writeBool()`、`writeBytes()`、`writeF32()`、`writeF64()`、`writeI8()`、`writeI16()`、`writeI32()`、`writeI64()`、`writeString()`
- `writeU8()`、`writeU16()`、`writeU32()`、`writeU64()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/network/`](../../../src/modules/network/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `network`。
