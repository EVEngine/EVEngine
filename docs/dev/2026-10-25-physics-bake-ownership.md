# Physics 烘焙功能归属调整

烘焙是 Physics 的能力。持久轨道、录制和播放器不应各自成为需要用户启动的模块。
本次将原来的十个 bake 模块收敛为一个可裁剪的 `physics_bake_graphics` 适配模块。

## 功能归属

- 现有 `physics_backend` 拥有不可变轨道、文件编解码、时钟和播放器，位于
  `physics/backend/bake`。它不链接求解器，也不拥有活动物理世界。
- Physics 拥有刚体采集和录制服务，脚本入口为 `physics.bake()` 与
  `physics.bakeRecording()`。Box2D 桥接随活动世界放在 Physics 中。
- Cloth、Rope 各自拥有 `source.captureBake(recording, seconds)`，共享的低层代码只处理
  值数据，不依赖这两个生产方。录制服务不向上引用 Cloth 或 Rope。
- Particles 拥有事件采集和 `particles.bakeRecording()`；通用的不可变粒子轨道仍由
  backend 实现。粒子生产方没有转移给 Physics。
- `physics_bake_graphics` 合并原来的原生呈现与脚本呈现，只依赖 graphics 和
  physics_backend。它不拉入 Physics、Cloth、Rope、Particles 或求解器。

旧脚本类型只保留无状态兼容外观，不再注册为模块。新调用使用宿主服务。
模块声明仍以 composed manifest 为唯一来源；这里说明决策，不维护第二份接口目录。

## 状态与生命周期

资产拥有不可变共享数据，每个显式创建的播放器拥有时间和放置状态。原生呈现器拥有
轨道与 CPU 缓冲区，调用者在绘制时传入匹配该资产的播放器；资产不匹配时先返回诊断。
脚本呈现器持有传入播放器的 VM 引用，准备呈现不会创建另一个时钟或重置进度。
借用的原生包装不能用于跨帧保留的脚本呈现；先调用 `prepareInstance()` 创建 VM 所有实例。

采集只在调用期间借用生产方，将结果复制为拥有数据的帧。播放器不保存活动世界、
发射器、布料或绳索指针。所有 VM 操作限制在 VM 所在线程；没有新增锁内回调。

## 成本、格式与验证边界

昂贵的录制、载入和准备保持显式步骤与 Result 诊断，播放继续使用准备好的缓冲区。
本次不改变持久格式、schema/version、缓存接纳规则或 Linux 原子发布协议。
不通过修改 CI 工作流降低验证要求。

原生纯回放、VM 纯回放和图形回放的裁剪 profile 分别验证不需要生产方/求解器的闭包；
完整宿主的采集测试验证它们与 Physics、Cloth、Rope、Particles 的组合。
呈现测试覆盖传入播放器的进度保留、共享控制、独立实例与异资产拒绝。
Linux 软件 Vulkan 的功能验证不能代替 Windows DLL、移动平台或真实 GPU 性能证据；
非 Linux 发布仍只承诺原子可见性，不承诺 Linux 的目录持久性屏障。
