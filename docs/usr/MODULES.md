# 模块使用手册

本目录按模块说明 EVEngine 面向 Squirrel 游戏脚本公开的能力。每篇包含入口、最小示例、至少两个目标导向任务、当前绑定的 API 快查和生命周期注意事项。

首次查阅前请阅读 [API 使用约定](API-CONVENTIONS.md)，了解模块对象、辅助对象、生命周期与“C++ public 方法不一定是脚本 API”的边界。
本轮逐章核对的实现、测试证据与结论记录在 [用户模块文档 Review](REVIEW.md)。

## 运行环境与输入

- [窗口](modules/window.md)：创建和配置单一游戏窗口。
- [事件](modules/event.md)：泵送平台事件，并用字符串消息队列在模块或线程之间传递通知。
- [计时器](modules/timer.md)：读取启动后的高精度时间和帧间隔。
- [系统信息](modules/system.md)：查询操作系统、CPU、内存、电量、剪贴板和 GPU 信息。
- [键盘](modules/keyboard.md)：查询按键状态、键盘重复和文本输入。
- [鼠标](modules/mouse.md)：查询鼠标位置、按键和指针可见状态；当前脚本绑定不含相对模式设置。
- [触摸](modules/touch.md)：按索引读取当前触点数量和归一化/屏幕坐标。
- [手柄](modules/joystick.md)：查询手柄数量并管理 SDL GameController 映射；当前绑定不含轴和按钮读取。

## 资源与 I/O

- [文件系统与热重载](modules/filesystem.md)：通过统一虚拟文件系统读写、枚举、挂载并监视游戏资源。
- [数据](modules/data.md)：管理 ByteData/DataView、压缩、哈希以及 JSON/XML 文档。
- [图像](modules/image.md)：说明图像模块的脚本绑定边界，以及通过字体字形获得 ImageData 的可用路径。
- [字体](modules/font.md)：从文件或内存创建 FontData，并读取字形度量和位图。
- [本地化](modules/i18n.md)：按语言载入 JSON 翻译表、点号键取值、占位符与复数规则、默认语言回退。
- [声音数据](modules/sound.md)：读取声音数据、解码器和采样信息，供 Audio 创建 Source。
- [音频播放](modules/audio.md)：创建可播放 Source，控制音量、音高、循环、进度和 3D 声源位置。
- [3D 模型](modules/model3d.md)：通过 medialoader/Assimp 载入模型数据，再交给图形模块渲染。
- [网络](modules/network.md)：提供 HTTP 请求、TCP 客户端/服务端和基础网络状态。

## 游戏玩法

- [脚本 ECS](modules/entity.md)：通过 Component、Entity 和 System 声明数据组合与批量更新逻辑。
- [物理（Box2D / Box3D）](modules/physics.md)：2D World/Body/Fixture（像素坐标 + meter）与 3D World3D/Body3D/Shape3D（米）；接触事件与 `rayCast` / `queryAABB` / `testPoint` 拾取查询。
- [Tilemap](modules/map.md)：创建或载入 TileLayer，设置瓦片、投影、图层并提交渲染。
- [粒子](modules/particles.md)：用代码或 JSON 创建发射器，配置运动、颜色、寿命并进行更新和渲染。
- [动画](modules/animation.md)：Tween 补间、3D 骨骼播放（状态机 / Motion Matching）、控制论程序动画（`ControlAnim` / `ControlPose`）、以及拖尾轨迹（`AnimTrail`）。
- [RPG 系统](modules/rpg.md)：组合属性、效果、状态、技能、施法与伤害结算。
- [背包 / 物品栏](modules/inventory.md)：物品定义、背包容器、转移、装备栏与可插拔接纳/容量/堆叠规则。
- [建筑放置](modules/building.md)：策略 / 经营类建筑定义、格子占用、鬼影预览与可插拔校验/吸附。
- [程序化生成](modules/procgen.md)：按算法名和 Params 生成网格、地图层、图像、法线图或 GPU 纹理。

## 表现与场景

- [图形渲染](modules/graphics.md)：清屏、2D 图元、纹理、Canvas、摄像机和 3D 渲染；Camera2D/3D 提供屏幕↔世界与拾取射线。
- [昼夜循环](modules/daynight.md)：随时间驱动的太阳轨道、程序化天空盒（IBL），以及月光 / 星光 / 火焰 / 萤火虫等夜间光照系统。
- [天气系统](modules/weather.md)：实时降水 / 闪电 / 风场，含雨、雪、雷暴预置与风暴氛围。
- [声明式 UI](modules/ui.md)：构建并挂载保留式控件树，通过稳定 ID 消费点击和更改事件。
- [声明式场景树](modules/scene.md)：用节点描述构建 2D/3D 层级，维护 local/world 变换并连接渲染实体。

## 高级能力

- [数学](modules/math.md)：提供向量、矩阵、2D/3D 拾取与重叠几何测试、噪声、随机数、插值和缓动工具。
- [空间索引](modules/spatial.md)：四叉树 / 八叉树 / 空间哈希 / 空间二分树，用于地图与场景的快速裁剪与邻近查询。
- [编辑器构件](modules/editor.md)：3D 变换 gizmo、地图笔刷、Toolbar/Inspector/Dock/History；用于组装自定义编辑器，而非内置完整编辑器。
- [逆运动学](modules/ik.md)：创建 2D/3D 骨架和 FABRIK Solver，设置目标并逐帧求解。
- [GPU 计算](modules/gpgpu.md)：创建 storage buffer 和 compute shader，绑定后调度 Vulkan compute。
- [张量](modules/tensor.md)：执行 eager 张量运算，或用 Func 构图、编译并重复运行。
- [线程与异步](modules/thread.md)：使用线程池执行原生安全任务，通过 Channel/Event 把结果送回主线程。
- [原生插件](modules/plugins.md)：从动态库加载用 EVEngine SDK 编译的原生模块。
- [内置演示](modules/demo.md)：查询或运行随宿主编译的演示能力，用于验证引擎安装。
