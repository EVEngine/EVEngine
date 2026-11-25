# 房主 P2P 联机

`PeerRoom` 是网络模块内的房间协议，`UdpRoom` 用现有 `NetHost` / `UdpLink`
接入真实 UDP。脚本入口是 `eve.PeerRoom()`。目录由 `network` 的递归源码扫描编译，
不注册第二个模块，也不引入独立 boot list。

支持 2–16 人（包括房主），加入、准备、开局、可靠有序消息广播、离开和房主断线。
房主本身也是玩家，其他玩家通过房主交换消息。房间满员或已经开局会明确拒绝加入。
这是可直连的房主拓扑，不包含全连接 mesh、NAT 穿透、中继、匹配、房主迁移或自动重连。
地址身份来自传输层，不是账号认证；UDP 层不提供加密。

## module-interface: network.multiplayer

- **权威 owner**：房主 `PeerRoom` 独占成员列表、准备状态和开局决定；客户端持有只读投影。
  游戏拥有世界状态、实体和输入验证；本模块不复制 ECS，也不建立统一玩家实体根。
- **能力输入/输出**：无新增 capability。`UdpRoom` 显式借用已有 `Network&`，没有可选 provider
  或静默 fallback。会话协议与传输通过 owning `RoomPacket` 组合，1:1 显式调用，无热路径查找。
- **数据/Link 面**：`PlayerId{room,value}` 是跨进程逻辑身份，不是 ECS 或内存句柄。
  房间 nonce 和加入 nonce 由调用者的命名 RNG 流提供，必须非零且跨会话唯一。
  成员编号单调分配且不复用；`PeerId` 是传输分配的连接身份，调用者不得在同一房间复用。
- **事件面**：有界 owning `RoomEvent` 队列，单消费者轮询，无外部回调；成员变化发布完整名单。
  `Message` 附发送者、模拟 tick 和原始二进制 payload。房主检查发送者与连接一致后再转发。
  游戏必须自行验证输入内容、权限和 tick 窗口；收到消息不等于批准游戏操作。
- **脚本面**：`PeerRoomBindings.cpp` 注册 `eve.PeerRoom`。可失败操作使用公共
  `script::projectResult`，不使用 bool/lastError。uint64 身份和接收 tick 投影为十进制字符串，
  避免 Squirrel 有符号整数截断；脚本提交 nonce/tick 限定为正数/非负 int64。
- **工具协议面**：无新增 MCP 工具。wire schema 是 `EVMP` v1，使用 little-endian：
  magic(4)、version(u8)、opcode(u8)、room(u64)、nonce(u64)、player(u32)、tick(u64)、
  payloadSize(u32)、payload。固定头 38 字节。未知版本/类型、尾随字节、非法名单均拒绝。
  v1 不做跨版本迁移；新版本只能通过新握手建立新房间。
- **成本/热路径**：`PeerRoom.cpp`、`UdpRoom.cpp` 标记 `eve-hot-path`。
  单条游戏消息最多 4096 字节；输入/输出事件各 256 项；适配器输入 256 项；
  每条可靠 UDP 链路最多提交 64 条未确认消息。房间队列满返回 Conflict，且不部分修改状态。
  适配器溢出明确 Failed，需关闭并重建，不能静默丢失后继续游戏。
- **生命周期/时间**：所有调用在 Network owner 线程，无锁内脚本调用。
  先销毁 UdpRoom，再销毁 Network；Pimpl 按 link→socket 顺序释放。
  UdpLink 析构按实例身份注销注册，旧实例不能注销同 socket 上的新实例。
  房主失联关闭客户端；没有选主。销毁不隐式提交离开操作；显式 close 后继续 pump，
  直到 pendingSends 为零或调用者的截止时间到达。零表示传输工作排空，不保证对端游戏已处理。
  房间不读取 wall clock，tick 原样转发；底层 UDP 重传/心跳使用已有 Network 时钟。
  相同 nonce 和相同有序输入产生 bit-exact 协议输出，实际网络到达顺序不保证确定性。
- **恢复/热重载**：没有持久化或中途恢复接口。结束旧房间并生成新 nonce 后重新握手；
  不保留跨重载引用或借用 payload。旧房间数据不允许进入新房间。
- **裁剪/依赖**：属于现有 L1 `network`，复用其 common/data/platform_event/Poco 依赖；
  不依赖 scene、graphics、physics 或具体玩法模块。本改动不宣称已验证 L2/L3/L4 裁剪。

## 接入

C++ 在 Network 生命周期内构造 `UdpRoom room(network)`，调用 `host(port, nonce, capacity)`
或 `join(address, port, nonce)`，检查每个 Result。每个网络帧调用 `pump()` 并排空
`room().pollEvent()`；在 lobby 调用 `setReady`，所有人准备后由房主调用 `start`。
playing 状态可 `send(tick, bytes)`，广播会回送给原发送者以便所有玩家走相同处理路径。

脚本示例及事件格式见 [网络手册](../../../../docs/usr/modules/network.md)。

## 验证入口

- `test/unit/network/multiplayer_tests.cpp`：三人组合、身份伪造、截断/旧协议、
  nonce 关联、容量拒绝、准备开局、广播、断线、关闭、队列背压。
- `test/integration/network/network_udp_lifetime_tests.cpp`：真实 loopback UDP 三人路径，
  以及 link 销毁后继续 pump 的生命周期回归。
- `make check`；`make build/win32-debug`；按需 `make test/win32-debug DOMAIN=network`。
  不在本机运行全引擎测试套件。
