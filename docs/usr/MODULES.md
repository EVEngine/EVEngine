# 模块使用手册

[English](../en/usr/MODULES.md)

本目录按模块说明 EVEngine 面向 Squirrel 游戏脚本公开的能力，重点是入口、示例、游戏开发任务与生命周期注意事项。
完整 C++ 接口由 [Doxygen](https://evengine.github.io/EVEngine/) 自动生成；精确的脚本签名查 SDK 的
`share/eve/ai/eve-api.json`、编辑器补全或 MCP `eve_api_search` / `eve_api_get`。不再重复维护只有方法名的长清单。

> 使用前提：从[官网发布页](https://github.com/EVEngine/EVEngine/releases)下载并解压对应平台的 SDK（无需编译引擎），然后用 `eve run <游戏目录>` 运行；本手册适用于 SDK 中的全部模块。

模块可以按需裁剪：见 [按需裁剪模块](TRIMMING.md)。脚本里用 `has_module("slot")` 判断某个模块在当前构建中是否存在（下载的 SDK 默认包含全部模块）。

首次查阅前请阅读 [API 使用约定](../dev/API-CONVENTIONS.md)，了解模块对象、辅助对象、生命周期与“C++ public 方法不一定是脚本 API”的边界。
本轮逐章核对的实现、测试证据与结论记录在 [用户模块文档 Review](https://github.com/EVEngine/EVEngine/blob/main/docs/usr/REVIEW.md)。

新项目先读[完整游戏开发流程](GAME_DEVELOPMENT.md)：从配置、输入和状态，到资源、HUD、
存档、调试和交付。语言基础见 [EveScript](EVESCRIPT.md)。章节中的“接线片段”需要已有
世界/资源；标注“完整 main.nut”的示例才可直接替换入口。脚本接口以绑定为准，不以 C++
同名函数的返回类型推断。高级专题与按类分组的参数参考在各章节末尾。

## 运行环境与输入

- [Agent](modules/agent.md)：通用策略学习、动作决策、tensor 推理、Squirrel 接口与轨迹回放。
- [窗口](modules/window.md)：创建和配置单一游戏窗口。
- [事件](modules/event.md)：泵送平台事件，并用字符串消息队列在模块或线程之间传递通知。
- [响应式编程](modules/rx.md)：UniRx 风格推送流，Subject 家族 + LINQ 操作符 + ReactiveProperty + 事件桥接。
- [计时器](modules/timer.md)：读取启动后的高精度时间和帧间隔。
- [OS](modules/os.md)：查询操作系统、CPU、内存、电量、剪贴板和 GPU 信息。
- [键盘](modules/keyboard.md)：查询按键状态、键盘重复和文本输入。
- [鼠标](modules/mouse.md)：查询鼠标位置、按键、相对模式、抓取和指针可见状态。
- [触摸](modules/touch.md)：按索引读取当前触点数量和归一化/屏幕坐标。
- [手柄](modules/joystick.md)：查询手柄数量、读取轴/按钮/方向帽、管理 SDL GameController 映射，并支持振动。

## 资源与 I/O

- [文件系统与热重载](modules/filesystem.md)：通过统一虚拟文件系统读写、枚举、挂载并监视游戏资源。
- [数据](modules/data.md)：管理 ByteData/DataView、压缩、哈希以及 JSON/XML 文档。
- [图像](modules/image.md)：加载/缓存图像、创建像素缓冲、旋转和编辑像素，再上传 GPU 纹理。
- [字体](modules/font.md)：从文件或内存创建 FontData，并读取字形度量和位图。
- [本地化](modules/i18n.md)：按语言载入 JSON 翻译表、点号键取值、占位符与复数规则、默认语言回退。
- [声音数据](modules/sound.md)：读取声音数据、解码器和采样信息，供 Audio 创建 Source。
- [音频播放](modules/audio.md)：创建可播放 Source，控制音量、音高、循环、进度和 3D 声源位置。
- [3D 模型](modules/model3d.md)：通过 medialoader/Assimp 载入模型数据，再交给图形模块渲染。
- [网络](modules/network.md)：提供 HTTP 请求、TCP 客户端/服务端和基础网络状态。
- [数据库](modules/database.md)：SQLite 连接、JSON 行接口与轻量 ORM，用于存档与配置表。

## 资源管线与格式

- [资源导入与 Cook](modules/asset.md)：源包、验证、平台 Cook 与运行时加载。
- [Unity 资源导入](modules/asset-unity-import.md)：扫描与有限转换，明确不能转换的行为。
- [Schema](modules/schema.md)：注册结构、精确版本校验和失败诊断。
- [内容定义](modules/definitions.md)：版本化注册、热替换、generation 与 stale handle。

## 存档

- [跨模块存档用法](SAVE.md)：用 `RPGSaveSession` / `InventorySaveSession` / tactics 快照拼玩家存档（0.6 推荐编排）。

## 游戏玩法

- [属性](modules/attributes.md)：基础属性与来源可追踪的修饰，避免装备/效果重复应用。
- [资源经济](modules/economy.md)：玩家账本、容量、消费、溢出与采集边界。
- [条件树](modules/condition.md)：可解释 Compare / PolicyCall AST（C++）。
- [决策](modules/decision.md)：黑板、状态机与空间采样（`eve.Decision()`，由 `npc_ai` 托管）。
- [标签](modules/tags.md)：主体标签、能力与层级 GameplayTag 的区别。
- [游戏事件日志](modules/game_event.md)：持久事件、消费者游标与快照。
- [策略目录](modules/policyregistry.md)：元数据发现与选择，脚本自己管理回调。
- [事务](modules/transaction.md)：跨参与者准备/提交和失败原子性；C++ 契约章节。
- [RTS](modules/rts.md)：世界、阵营、单位命令、经济/生产组合与固定步进。
- [动作战斗](modules/combat.md)：行动协议与命中、伤害、属性结算的组合。
- [战棋](modules/tactics.md)：棋盘拓扑、回合、命令与快照回放。
- [群体移动](modules/crowd.md)：流场、拥挤避让与单位移动。
- [感知](modules/sensing.md)：空间查询与感知记录。
- [涌现规则](modules/emergence.md)：按脏键触发条件和延迟行动。
- [绳索](modules/physics_rope.md)、[布料](modules/physics_cloth.md)、[轨迹](modules/physics_trajectory.md)：独立物理运行时的创建/步进/清理。
- [流体](modules/fluids.md)：二维与三维求解器选择；[Fluid3D](fluid3d.md) 提供 Result 接线示例。
- [Procgen–Physics](modules/procgen_physics.md)、[Procgen–Animation](modules/procgen_animation.md)：生成结果接入碰撞与动画。
- [PixelWorld–Graphics](modules/pixelworld_graphics.md)、[PixelWorld–Physics](modules/pixelworld_physics.md)、[PixelWorld–Thread](modules/pixelworld_thread.md)：表现、碰撞和候选调度的可选组合。

- [脚本 ECS](modules/entity.md)：通过 Component、Entity 和 System 声明数据组合与批量更新逻辑。
- [物理（Box2D / Box3D）](modules/physics.md)：2D World/Body/Fixture（像素坐标 + meter）与 3D World3D/Body3D/Shape3D（米）；接触事件与 `rayCast` / `queryAABB` / `testPoint` 拾取查询。
- [几何破碎 Destruction](modules/physics_destruction.md)：预切几何集合、连接图与场驱动断边；可选 [DestructionFx](modules/physics_destruction_graphics.md) 骨块绘制与 Sleep 合批。
- [Tilemap](modules/map.md)：创建或载入 TileLayer，设置瓦片、投影、图层并提交渲染。
- [粒子](modules/particles.md)：用代码或 JSON 创建发射器，配置运动、颜色、寿命并进行更新和渲染。
- [像素物质世界](modules/pixelworld.md)：64×64 分块的确定性沙、水、火焰与可破坏二维材料仿真。
- [PixelWorld 确定性重放](modules/pixelworld_replay.md)：命令日志、周期 checkpoint 与首个 Tick/Chunk 分歧定位。
- [PixelWorld 兴趣区 Streaming](modules/pixelworld_streaming.md)：Chunk 兴趣区、增量更新、驱逐 tombstone 与权威校正。
- [PixelWorld Catalog 编辑器](modules/pixelworld_editor.md)：可挂载的材质浏览、属性、反应与相变规则事务式编辑面板。
- [动画](modules/animation.md)：Tween 补间、3D 骨骼播放（状态机 / Motion Matching）、控制论程序动画（`ControlAnim` / `ControlPose`）、受击晃动与平衡恢复（`PhysicalBalancePose`）、以及拖尾轨迹（`AnimTrail`）。
- [攀爬与跑酷](modules/climbing.md)：前方障碍/顶部落点探测、确定性动作选择，以及受 Physics 胶囊体约束的 vault / mantle 执行。
- [行动与 Ability 协议](modules/action.md)：脚本拥有的 `ActionRuntime` / `AbilityRuntime`；grant、冷却、激活与直接 action 提交。
- [通用结算](modules/settlement.md)：确定性结算管线与规则文档；脚本 ledger runtime 与各玩法域 `configureSettlementRulesJson`。
- [NPC AI](modules/npc_ai.md)：层级状态树、黑板、感知记忆与确定性调度的脚本世界入口。
- [RPG 系统](modules/rpg.md)：组合属性、效果、状态、技能、施法与伤害结算。
- [背包 / 物品栏](modules/inventory.md)：物品定义、背包容器、转移、装备栏与可插拔接纳/容量/堆叠规则。
- [卡牌游戏工具](modules/cardgame.md)：扇形手牌布局、抽牌/洗牌、悬浮放大、拖拽到落牌区、敌方手牌与费用置灰（参考 UiCard）。
- [建筑放置](modules/building.md)：策略 / 经营类建筑定义、格子占用、鬼影预览与可插拔校验/吸附。
- [建筑可视化](modules/buildingfx.md)：把放置世界和鬼影同步为 2D/3D 视觉并绘制放置网格。
- [程序化房屋](modules/housegen.md)：数据驱动的房屋生成（组件库 + 请求 → 布局 JSON）。
- [建筑空间 ArchSpace](modules/archspace.md)：Pascal 风格的楼层/墙/房间/开口文档编辑（与 building 放置域分离）。
- [程序化生成](modules/procgen.md)：按算法名和 Params 生成网格、地图层、图像、法线图或 GPU 纹理。
- [六边形地图](modules/hexmap.md)：可编辑的尖顶六边形格网（平面与球面两种后端），按 5×5 分块生成地形/水面/河流/道路/城墙/地物与迷雾网格，附寻路、单位移动和存档。
- [武器系统](modules/weapon.md)：数据驱动的武器、弹药池、挂点、瞄准、开火、装填与事件查询。
- [载具系统](modules/vehicle.md)：载具定义、移动命令、座位和炮塔、伤害以及 2D/3D 物理接入。
- [通用效果](modules/effects.md)：按主体管理可堆叠、可过期的效果及其结构化载荷与事件。
- [原子状态补丁](modules/statepatch.md)：用带冲突检查的批次原子修改 JSON 状态，并支持脏键、事件和快照。
- [通用命令队列](modules/orders.md)：确定性优先级命令、抢占、超时、载荷、生命周期事件和快照。
- [社会关系图](modules/social.md)：所有权、控制权、角色指派和带权有向关系。
- [生产队列](modules/production.md)：按 owner 分配并行槽位，确定性推进可暂停、恢复的生产任务。
- [权限规则](modules/authority.md)：按 actor、scope 和 capability 仲裁有优先级、可过期的允许/拒绝规则。

## 表现与场景

- [HD2D](modules/hd2d.md)：像素角色与三维舞台组合。
- [贴花](modules/decal.md)：投影、纹理与烘焙工作流。
- [Scene–Physics](modules/scene_physics.md)：生成碰撞体接入游戏拥有的物理世界。

- [图形渲染](modules/graphics.md)：清屏、2D 图元、纹理、Canvas、摄像机和 3D 渲染；Camera2D/3D 提供屏幕↔世界与拾取射线。
- [实时雾气](modules/graphics_fog.md)：MAC 流体、世界空间光线步进、Froxel 积分与解析体积光；艺术层不回写密度。
- [体素](modules/voxel.md)：32³ chunk 体素世界，贪婪矩形合并 + 实例化渲染、跨 chunk 接缝消隐、顶点 AO、DDA 射线拾取、自动流式地形生成与存档。
- [Sprite-Stacking](modules/spritestack.md)：把 3D 模型切成多层 RGBA 图，以叠片方式渲染成伪 3D 物体（经典 billboard 切片 / 水平俯视切片）。
- [昼夜循环](modules/daynight.md)：随时间驱动的太阳轨道、程序化天空盒（IBL），以及月光 / 星光 / 火焰 / 萤火虫等夜间光照系统。
- [天气系统](modules/weather.md)：实时降水 / 闪电 / 风场，含雨、雪、雷暴预置与风暴氛围。
- [天气](modules/weather.md)及其[可交互积雪服务](modules/snow.md)：降水、风、雷暴，以及深度场积雪的真实位移、POM 微细节与降雪回填。
- [Avatar 分层渲染](modules/avatar.md)：Image 图层 / Live2D / VRoid 立绘，表达式、口型与动作。
- [对话与剧情](modules/dialogue.md)：角色舞台、打字机、选项分支、口型与台词池（.dnut）。
- [3D 相机控制器](modules/camera.md)：跟随 / 环绕 / 俯视 / 第一人称 / 过场视角序列。
- [场景加载器](modules/sceneloader.md)：glTF / OBJ / FBX 解码成声明式场景树，支持热重载与异步加载。
- [声明式 UI](modules/ui.md)：构建并挂载保留式控件树，通过稳定 ID 消费点击和更改事件。
- [声明式场景树](modules/scene.md)：用节点描述构建 2D/3D 层级，维护 local/world 变换并连接渲染实体。

## 高级能力

- [性能分析](modules/profiler.md)：捕获 CPU/GPU 帧成本并比较同场景结果。
- [GPU Agents](modules/gpuagents.md)：GPU 群体模拟与渲染接线。
- [Agent–Tensor](modules/agent_tensor.md)：CPU/GPU 学习与推理提供者，无隐式回退。

- [数学](modules/math.md)：提供向量、矩阵、2D/3D 拾取与重叠几何测试、噪声、随机数、插值和缓动工具。
- [空间索引](modules/spatial.md)：四叉树 / 八叉树 / 空间哈希 / 空间二分树，用于地图与场景的快速裁剪与邻近查询。
- [编辑器构件](modules/editor.md)：3D 变换 gizmo、地图笔刷、Toolbar/Inspector/Dock/History；用于组装自定义编辑器，而非内置完整编辑器。
- [逆运动学](modules/ik.md)：创建 2D/3D 骨架和 FABRIK Solver，设置目标并逐帧求解。
- [GPU 计算](modules/gpgpu.md)：创建 storage buffer 和 compute shader，绑定后调度 Vulkan compute。
- [张量](modules/tensor.md)：执行 eager 张量运算，或用 Func 构图、编译并重复运行。
- [线程与异步](modules/thread.md)：使用线程池执行原生安全任务，通过 Channel/Event 把结果送回主线程。
- [风格化渲染](modules/stylize.md)：卡通 / 水彩 / 水墨 / 像素后处理与网格着色。
- [虚拟几何](modules/virtualgeometry.md)：GPU 簇裁剪 + LOD 的大场景几何管线。
- [统一网格](modules/grid.md)：格子 ↔ 世界坐标换算与拓扑（纯 C++，被 map/building 消费）。
- [原生插件](modules/plugins.md)：从动态库加载用 EVEngine SDK 编译的原生模块。
- [内置演示](modules/demo.md)：查询或运行随宿主编译的演示能力，用于验证引擎安装。

## 编辑器与可编辑目标卫星

游戏运行时与内容制作工具分别选择模块；目标借用的资源需比会话活得更久。

- [动作时间线编辑：action_editor](modules/action_editor.md)
- [动画片段编辑：animation_editor](modules/animation_editor.md)
- [神经重定向提供者：animation_tensor](modules/animation_tensor.md)
- [声音资源与试听编辑：audio_editor](modules/audio_editor.md)
- [角色文档编辑：avatar_editor](modules/avatar_editor.md)
- [反射探针可视化：graphics_editor](modules/graphics_editor.md)
- [关卡瓦片笔刷：level_editor](modules/level_editor.md)
- [瓦片层可编辑目标：tilelayer_target](modules/tilelayer_target.md)
- [生态规则编辑：biome_editor](modules/biome_editor.md)
- [程序化脚本与网格编辑：procgen_editor](modules/procgen_editor.md)
- [高度场可编辑目标：heightmap_target](modules/heightmap_target.md)
- [场景文档与实时编辑：scene_editor](modules/scene_editor.md)
- [UI 主题与预览编辑：ui_editor](modules/ui_editor.md)
- [体素模型目录编辑：voxel_editor](modules/voxel_editor.md)
- [体素世界可编辑目标：voxelworld_target](modules/voxelworld_target.md)
- [平台事件：帧循环拥有输入泵](modules/platform_event.md)
- [光线追踪能力与降级路径](modules/graphics_raytracing.md)
- [动作驱动的风格化特效](modules/stylize_action.md)
