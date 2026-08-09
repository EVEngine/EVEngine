这些都是可以载入的module，不需要时可以不载入
全是等待被调用的模块，不会主动去调用

主要参考love2d的设计，但也要将cocos2dx中的一些优秀设计拿过来

## 类绑定原则

每个API都不重名，不使用重载，使用明确的类型，有部分参数有默认值

考虑一下枚举类型的接口
可以使用 uint32_t 或直接用 string

经过考虑，我计划使用string，主要原因有3：
1. 方便，无需定义枚举类型，少了很多绑定操作的费事定义
2. 可扩展，日后api想扩展新功能，无需导出新的定义，只需在函数增加对新名称的支持即可
3. 直观，不但脚本中直观，C++代码中用string也很直观地知道这是啥，避免了用宏，枚举等需要引入头文件，解决namespace等诸多问题


## 模块设计

1. 文件系统 - filesystem
    封装了核心的文件系统功能，提供如下功能：统一的文件系统访问、目录查看修改、监视文件变动、文件读取
    主实现：physfs；cppfs::File 仅用于 OS 绝对路径特例
    watch API（抽象虚函数）：`watch` / `unwatch` / `pollWatch` / `getLastWatchPath`
    watch 实现：physfs 后端 + Poco DirectoryWatcher（FileWatch）

2. 事件系统 - event
    主要处理多线程间的信号同步与各种回调事件，处理来自硬件的各种信号

3. 窗口系统 - window
    主要处理渲染窗口的设置、位置、样式、全屏等，我们只支持单窗口

4. 图形系统 - graphic
    主要处理图形的各种渲染、显卡、shader等，是核心中的核心，并且相比love2d, 拥有更多高级功能
    有如下主要功能：
    a. 精灵类，根据属性，自动渲染不同样式的图片
    b. 场景类，可以传入参数的场景，方便调试游戏的部分模块，部分章节
    c. 地图类接口，可以直接渲染tilemap
    d. 摄像机，可以做2d下的光照和法线贴图
    e. 低像素优化，支持小图片的旋转

5. 插件系统 - plugins
    主要负责加载第三方模块和插件，加载dll等

6. 管理系统 - system
    负责系统版本、时间、状态等的查询和设置
    Module：`System`（`getOS` / `getProcessorCount` / `getSystemRAM` / `getProcessMemoryMB` /
    `getWallTime` / `sleepMilliseconds` / 剪贴板 / 电量 / GPU 查询）
    帧计时仍用 `Timer`；GPU 信息在 Graphics init 后可用

6b. 数学库 - math（`eve.Math`）
    Vec2 / Vec3 / Mat4（glm）；标量与几何工具；noise1/2/3；贝塞尔；可种子随机数
    代码：`src/modules/math/`

6b2. 逆运动学 - ik（`eve.IK`）
    封装 [ik.hpp](https://github.com/sunxfancy/ik.hpp)（`external/ik.hpp`）：Skeleton2D/3D、Solver2D/3D（FABRIK）
    `newSkeleton2D` / `newSolver2D` / `createBone` / `addTarget` / `solve` / `step`
    代码：`src/modules/ik/`

6c. 张量计算 - tensor（`eve.TF` / 类型 `Tensor`）
    TF2 风格：默认 eager；`tf.func()` 建图 → `compile` / `run*`（对应 `tf.function`）
    模块级 `add`/`multiply`/`matmul`/`relu`/`reduceSum`/`where`；`getDevice()` → `"cpu"`
    代码：`src/modules/tensor/`

7. 资源管理系统 - image sound video font(`font`/`Font`) 3dmodel(`model3d`/`Model3D`) animation
    负责加载图形、声音、视频、字体、3d模型、动画等不同格式数据
    3dmodel：`Model3D::newModelData` / `newModelDataFromFile` → `ModelData`（medialoader Assimp）
    font：`Font::newFontData` / `newFontDataFromFile` → `FontData`（FreeType；glyph → ImageData）

8. 网络系统 - network
    封装网络通讯的各种基础操作

9.  线程系统 - thread（`eve.Thread`）
    负责线程池、异步 Task、跨线程 ThreadChannel（字符串消息）
    `getPool` / `newThreadPool` / `getChannel` / `newChannel` / `postMain`
    `ThreadPool::submitSleep` / `submitPush` / `submitPost` / `waitAll`；worker 勿碰 Squirrel VM
    脚本异步：`src/scripts/async.nut`（Promise / nextTick / setTimeout / asyncSleep / asyncDelay）
    帧循环 `async_pump()`；`.fail` 代替 JS 的 `.catch`（Squirrel 关键字）

10. 粒子系统 - particles  
   ECS `ParticleEmitter` + Sim/Render/Config System；JSON 热更；Camera2D/Canvas；发射区域与径向/切向力。
    ECS：`ParticleEmitter`（Config/Sim/Draw/Resource）
    System：`ParticleConfigSystem`（filesystem watch + mtime 回退）/ `ParticleSimSystem` / `ParticleRenderSystem`
    Module：工厂、`newEmitterFromFile`、脚本绑定；`update` 含 poll+sim

11. Box2d物理 - box2d（脚本模块名 `Physics`）
    负责管理box2d物理引擎
    Module：`Physics`（`setMeter` / `newWorld`）
    类型：`World` / `Body` / `Fixture`；坐标为像素，内部按 meter 换算
    帧循环：`world.update(dt)`；碰撞事件：`begincontact` / `endcontact` → `event`
    可选：`world.drawDebug(gfx)`
    代码：`src/modules/physics/`（避免与第三方 `Box2D/` 在大小写不敏感文件系统上冲突）

11b. 声明式场景树 - scene（`eve.Scene`）
    与 ui 同构：`SceneComponent.build` → `NodeDesc` → `SceneHost`；`mount` / `remountReconcile` / `beginBuild`
    `SceneNode`（GameObject）+ `TransformSystem`（local → world Mat4）；`space` 为 `"2d"`/`"3d"`
    可选 `linkRenderable2D/3D`：world TRS 同步到渲染实体
    场景即函数（props / children 插槽），非 Prefab
    代码：`src/modules/scene/`

11c. Avatar 分层渲染 - avatar（`eve.Avatar`）
    Image 图层叠加 / Live2D 可插拔后端 / VRoid（Model3D + Renderable3D）
    `newImageAvatar` / `newLive2DAvatar` / `newVroidAvatar`；`update` / `sync` / `render`
    设计：`docs/对话与Avatar模块设计.md`
    代码：`src/modules/avatar/`

11d. 对话框及脚本 - dialogue（`eve.Dialogue`）
    VN 舞台：角色、打字机、选项、槽位；剧情仍用 Squirrel（推荐 generator）
    `say` / `narrate` / `presentChoices` / `syncStage` / `bindAvatar`
    设计：`docs/对话与Avatar模块设计.md`；示例：`examples/dialogue/`
    代码：`src/modules/dialogue/`

12. 动画系统 - animation（`eve.Animation`）
    属性补间 Tween：`newTween(duration)` → `setFrom` / `setTo` / `setDelta`（相对差值）
    缓动 kind 与 Math.ease 一致；`setDelay` / `setRepeat` / `setYoyo`；角度 `set*Angle`
    帧循环：`anim.update(dt)`（或 `tween.update(dt)`）
    后期：帧动画 / Skeleton
    代码：`src/modules/animation/`

13. GPU计算系统 - compute
    并行化和异构计算，封装compute shader的相关操作

