这些都是可以载入的 module，不需要时可以不载入
全是等待被调用的模块，不会主动去调用

主要参考 love2d 的设计，但也要将 cocos2dx 中的一些优秀设计拿过来

权威声明在 `cmake/module_manifest*.cmake`（`eve_declare_module`）；裁剪见 `docs/dev/模块编排与裁剪架构.md`。
脚本用法见 `docs/usr/MODULES.md`；分项勾选进度见 `docs/dev/模块设计.md`。

## 类绑定原则

每个API都不重名，不使用重载，使用明确的类型，有部分参数有默认值

考虑一下枚举类型的接口
可以使用 uint32_t 或直接用 string

经过考虑，我计划使用string，主要原因有3：
1. 方便，无需定义枚举类型，少了很多绑定操作的费事定义
2. 可扩展，日后api想扩展新功能，无需导出新的定义，只需在函数增加对新名称的支持即可
3. 直观，不但脚本中直观，C++代码中用string也很直观地知道这是啥，避免了用宏，枚举等需要引入头文件，解决namespace等诸多问题

## 模块设计

按功能域列出 `src/modules/` 下的运行时模块。`*_editing` / `*_editor` 是编辑器卫星（文档/命令/面板），不逐条展开；引擎核心 `common` / `cmdline` / `devtools` 在 `src/engine/`，不是本目录。

### 运行时内核

1. 插件系统 — `plugins`（`eve.Plugins`）
    加载第三方动态库（dll / so），补注册模块。

2. 线程系统 — `thread`（`eve.Thread`）
    线程池、异步 Task、跨线程 ThreadChannel（字符串消息）
    `getPool` / `newThreadPool` / `getChannel` / `newChannel` / `postMain`
    脚本异步：`src/scripts/async.nut`（Promise / nextTick / setTimeout）；帧循环 `async_pump()`
    worker 勿碰 Squirrel VM

3. 计时器 — `timer`（`eve.Timer`）
    启动后高精度时间与帧间隔；帧计时用它，不用 HostSystem。

4. 管理系统 — `system`（`eve.HostSystem`，槽 `system`）
    `getOS` / `getProcessorCount` / `getSystemRAM` / `getProcessMemoryMB` /
    `getWallTime` / `sleepMilliseconds` / 剪贴板 / 电量 / GPU 查询
    脚本里 `eve.System` 是 ECS System 基类，不要混用。

5. 性能剖析 — `profiler`（`eve.Profiler`）
    按模块/zone 的 CPU 调用树；GPU 帧时间来自 Vulkan timestamp。关闭时零开销。

6. 数学库 — `math`（`eve.Math`）
    Vec2 / Vec3 / Mat4（glm）；标量与几何工具；noise；贝塞尔；可种子随机数；缓动

7. 统一网格 — `grid`
    格子 ↔ 世界坐标与拓扑（纯 C++，被 `map` / `building` 消费；无模块类）

### 平台与输入

8. 窗口系统 — `window`（`eve.Window`，槽 `win`）
    渲染窗口的设置、位置、样式、全屏；只支持单窗口

9. 平台事件 — `platform_event`（`eve.PlatformEvent`）
    泵送 SDL/硬件信号与跨模块字符串消息队列（原 `event` 已改名，与玩法 `game_event` 分开）

10. 响应式编程 — `rx`（`eve.Rx`）
    UniRx 风格推送流：`Subject` / `BehaviorSubject` / `ReplaySubject` / `ReactiveProperty`
    + LINQ（`map` / `filter` / `take` / …）+ `fromEvent` / `pump`
    设计：`docs/dev/superpowers/specs/2026-08-17-rx-module-design.md`

11. 键盘 / 鼠标 / 触摸 / 手柄 — `keyboard` `mouse` `touch` `joystick`
    输入状态查询；手柄含 SDL GameController 映射与振动

### 数据与 I/O

12. 文件系统 — `filesystem`（`eve.Filesystem` / `eve.HotReload`，槽 `fs` / `hot`）
    统一虚拟文件系统、目录、监视、读写
    主实现：physfs；cppfs::File 仅用于 OS 绝对路径特例
    watch：`watch` / `unwatch` / `pollWatch` / `getLastWatchPath`（physfs + Poco DirectoryWatcher）

13. 数据抽象 — `data`（`eve.DataModule`）
    ByteData / DataView、压缩、哈希、JSON / XML

14. 资源包 — `asset`
    运行时包准入、manifest、Cook IR；导入桥在更高层（`asset_import` / `asset_graphics` / `asset_scene` / `asset_procgen`）

15. 图片 / 字体 / 声音 / 音频 / 3D 模型 — `image` `font` `sound` `audio` `model3d`
    解码与播放：ImageData；FreeType → FontData；medialoader 音频 → OpenAL Source；Assimp → ModelData
    `ModelData` 可程序化改顶点法线（`applyVertexNormals` / `setVertexNormal`）并 `bakeNormalMap`
    视频资源尚未独立成模块

16. 本地化 — `i18n`（`eve.I18n`）
    JSON 翻译表、点号键、占位符、复数、默认语言回退、热重载

17. 网络系统 — `network`（`eve.Network`）
    HTTP、TCP 客户端/服务端、UDP；基础网络状态

18. 数据库 — `database`（`eve.Database`）
    SQLite（Poco Data）、JSON 行接口、轻量 ORM；存档与配置表

### 渲染与计算

19. 图形系统 — `graphics`（`eve.Graphics`，槽 `gfx`）
    核心渲染（Vulkan）：2D 图元、纹理、Canvas、摄像机、3D 前向/GBuffer、后处理
    声明式路径：ECS 组件 + `RenderSystem`；Love2D 风格即时模式仅作 C++/DevTools 逃生舱

20. 3D 相机 — `camera`（`eve.Camera`）
    跟随 / 环绕 / 俯视 / 第一人称 / 过场序列

21. GPU 计算 — `gpgpu`（`eve.Gpgpu`）
    Compute shader + storage buffer；与 Graphics 共用 device。原文档里的 `compute` 即此模块。

22. 张量计算 — `tensor`（`eve.TF`，类型 `Tensor`）
    TF2 风格：默认 eager；`tf.func()` 建图 → `compile` / `run*`；大图可走 `gpgpu`

23. 声明式 UI — `ui`（`eve.UI`）
    保留式控件树（React 式 build + dirty），挂 ECS `UIHost`

24. 风格化渲染 — `stylize`（`eve.Stylize`）
    NPR：cartoon / watercolor / ink / pixel；`StylePass` / `StyleChain` / 网格着色
    设计：`docs/风格化渲染模块设计.md`

25. 昼夜 / 天气 / 贴花 / 积雪 — `daynight` `weather` `decal` `snow`
    太阳与 IBL 天空；降水/闪电/风场；投影贴花；深度场积雪（脚印 / 弹坑 / 回填）

26. 体素 — `voxel`（`eve.Voxel`）
    32³ chunk、贪婪矩形合并、实例化绘制、视锥/距离裁剪
    设计：`docs/dev/体素渲染模块设计.md`

27. 虚拟几何 — `virtualgeometry`（`eve.VirtualGeometry`）
    GPU 簇裁剪 + LOD 的大场景几何管线

28. Sprite-Stacking — `spritestack`（`eve.SpriteStack`）
    把 3D 模型切成多层 RGBA，叠片伪 3D

29. HD-2D — `hd2d`（`eve.Hd2D`）
    TileLayer 挤出 3D 地形 + 摄像机朝向的角色/精灵 billboard

30. 粒子 — `particles`（`eve.Particles`）
    ECS `ParticleEmitter` + Config/Sim/Render System；JSON 热更；CPU 或 GPU 常驻模拟

31. 表面流体 — `fluids`（`eve.Fluids`）
    贴网格 SDF 的表面流体 + 屏空间重建（与 Physics 的 `Fluid2D` 不同）

### 世界与仿真

32. 声明式场景树 — `scene`（`eve.Scene`）
    与 ui 同构：`SceneComponent.build` → `NodeDesc` → `SceneHost`
    `SceneNode` + `TransformSystem`；可选 link 到 Renderable / Physics / Camera / Audio

33. 场景加载器 — `sceneloader`（`eve.SceneLoader`）
    glTF / OBJ / FBX → 声明式场景树；热重载与异步加载

34. Tilemap — `map`（`eve.Map`）
    TileLayer、Tiled JSON、多投影、对象层、寻路 / FlowField / FOV

35. 物理 — `physics`（`eve.Physics`）
    Box2D（像素 + meter）与 Box3D（米）；Cloth / Cloth3D / ClothGPU；`Fluid2D`
    代码在 `src/modules/physics/`（避免与第三方 `Box2D/` 大小写冲突）

36. 动画 — `animation`（`eve.Animation`，槽 `anim`）
    Tween；2D 帧动画 / Spine region；3D 骨骼（状态机 / Motion Matching）；ControlAnim；AnimTrail

37. 逆运动学 — `ik`（`eve.IK`）
    [ik.hpp](https://github.com/sunxfancy/ik.hpp)：Skeleton2D/3D、Solver2D/3D（FABRIK）

38. 空间索引 — `spatial`（`eve.Spatial`）
    QuadTree / Octree / SpatialHash / BSPTree：AABB 宽相查询
    设计：`docs/dev/空间索引模块设计.md`

39. 程序化生成 — `procgen`（`eve.Procgen`）
    算法注册表：地图 / 贴图 recipe / 网格（marching cubes 等）

40. 程序化房屋 — `housegen`（`eve.HouseGen`）
    组件库 + 请求 → 布局 JSON

41. 像素物质世界 — `pixelworld`（`eve.PixelWorldModule`）
    64×64 分块确定性沙/水/火等；配套 `pixelworld_graphics` / `pixelworld_physics` /
    `pixelworld_thread` / `pixelworld_replay` / `pixelworld_streaming` / `pixelworld_editor`

42. 群体 — `crowd`（`eve.Crowd`）
    连续流场寻路 + 海量单位转向；Boids；与渲染解耦

43. 转向 — `steering`（`eve.Steering`）
    独立转向力 / 行为，给单位移动用

### 玩法框架

44. 属性 / 效果 / 标签 / 定义 / Schema — `attributes` `effects` `tags` `definitions` `schema`
    数据驱动属性、可堆叠效果、GameplayTag、定义表、JSON schema

45. 决策 / 感知 / 动作 / 战斗 — `decision` `sensing` `action` `combat`
    条件求值、空间感知、中立动作生命周期、战斗结算（领域适配器挂上来）

46. 事务 / 状态补丁 / 权限 / 策略 — `transaction` `statepatch` `authority` `policyregistry`
    可回滚事务、带冲突检查的 JSON 补丁、actor/scope 权限、具名策略注册

47. 玩法事件 / 结算 — `game_event` `settlement`
    确定性玩法事件日志（不是平台输入）；结算流水线

48. 经济 / 命令队列 / 生产 / 社交 — `economy` `orders` `production` `social`
    资源账户；优先级命令队列；并行生产槽；关系图 / 所有权

49. RPG / 背包 — `rpg`（`eve.RPG`） / `inventory`（`eve.Inventory`）
    属性/效果/状态/技能/结算；物品、容器、装备栏

50. 卡牌 — `card`（`eve.Card`）
    手牌布局、抽洗、落牌区、费用等卡牌工具

51. 建筑 / 建筑可视化 — `building` `buildingfx`
    放置世界、鬼影、校验/吸附；2D/3D 视觉与放置网格

52. 武器 / 载具 — `weapon` `vehicle`
    武器定义、弹药、挂点、开火；载具移动、座位、炮塔；可选 Physics 适配

53. NPC AI / 攀爬 — `npc_ai` `climbing`
    数据向 NPC 编排；确定性攀爬/跑酷规划 + 胶囊约束执行

54. RTS / 战术 — `rts` `tactics`
    组合配置：RTS 命令/生产/属性；回合/格子战术棋盘

55. Avatar / 对话 — `avatar` `dialogue`
    Image / Live2D / VRoid 分层立绘；VN 舞台、打字机、选项（剧情仍用 Squirrel）
    设计：`docs/对话与Avatar模块设计.md`

### 编辑与属性协议

56. 属性访问 / 脚本模型 / 编辑契约 — `property_access` `scriptmodel` `editing`
    无渲染的属性契约、Squirrel 反射适配、可编辑 Target / 命令 / 事务（给编辑器与局内建造共用）

57. 编辑器构件 — `editor`（`eve.Editor`）
    不内置完整 3D/地图编辑器；可组装：TransformGizmo、TileBuffer+Brush、Toolbar/Inspector/Dock/History
    各领域另有 `*_editing`（文档模型）与 `*_editor`（挂到 Editor 壳）卫星模块
    设计：`docs/dev/编辑器模块设计.md`

58. 内置演示 — `demo`（`eve.Demo`）
    仅默认演示的程序化资源（音效/行星贴图）；非通用游戏 API
