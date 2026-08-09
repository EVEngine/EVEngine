# 网络模块

**脚本入口：** `eve.Network()`

提供 HTTP 请求、TCP 客户端/服务端和基础网络状态。

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

## 常见问题

- 提交 HTTP 后不 pump：请求完成不会被主线程处理。
- 发布环境关闭 TLS 校验：`setVerifySsl(false)` 只能用于受控调试。
- 假设 TCP 一次 send 对应一次 receive：应用层必须定义消息边界。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `accept()`、`add()`、`bind()`、`close()`、`closeAll()`、`connect()`、`get()`、`getName()`
- `getPeer()`、`getSocket()`、`isConnected()`、`listen()`、`newChannel()`、`newHttp()`、`newSession()`、`newTcp()`
- `newUdp()`、`pump()`、`remove()`、`send()`、`sendMsg()`、`sendMsgString()`、`sendString()`、`sendTo()`
- `sendToString()`、`setBody()`、`setBodyString()`、`setHeader()`、`setTimeout()`、`setVerifySsl()`、`submit()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/network/`](../../../src/modules/network/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `network`。
