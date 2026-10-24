# Physics Bake Graphics

`physics_bake_graphics` 是 L4 表现卫星，依赖 `physics_bake` 和 `graphics`，不依赖录制卫星或物理世界。
`submitRigidPlayback(player, identity, target)` 按持久身份采样米制轨道，将实例姿态写入
Renderable3D 真实矩阵通道，保留网格表现缩放；拒绝像素轨道、未知身份及非有限矩阵。
所有验证在修改前完成，失败保留渲染对象。直接 TRS setter 可退出矩阵模式。

调用只同步借用参数，不保存目标指针或创建实体；调用方每次提供有效的活对象，并排除
Scene 链接或其他变换写者。播放器或渲染对象任一先销毁均无反向引用。恢复后由调用方
重新解析活对象；本模块不创建持久 Link。接口不推进时间，不调用 solver，不上传网格。

## module-interface

- 原生输入/输出：同步提交函数、只读播放器、持久 ID、调用期借用的 Renderable3D，checked Result。
- Capability 输入/输出：无 provider 注册或查询。
- ECS 与 Link：只改调用方指定实体的 Transform3D，不枚举 View、增删实体或创建 Link。
- 事件：无回调或事件。
- 脚本：通过独立的 [physics_bake_graphics_script](physics_bake_graphics_script.md) 卫星提供。
- 工具协议：无。

热路径已登记。组合测试覆盖实际 modelFromTransform 消费及双向销毁，不代表 GPU 画面验收。
完整 manifest 图形播放裁剪见下文；2D 表现适配见下文。

2D 重载接受 `Renderable2D`，仅消费 `PlanarPixels`；像素 XY 保持原轴，Z 四元数转为
渲染接口要求的角度制，原有 sprite、尺寸与 XY 缩放保持不变。3D 米制轨道明确拒绝。
两个重载都只修改已验证目标的变换值，不保留跨帧引用。平面旋转采用最短插值，不能恢复多圈运动。

## 变形网格

`DeformMeshPresentation::prepare(track, ropeRadius)` 拥有独立播放器和可复用 CPU 缓冲；
米制三角拓扑生成双面网格与当前法线，线段拓扑生成带端盖的八边形管段，断开部分不补连。
输出顶点/索引预算在分配前检查。`player()` 只供时间和变换控制，不得移动或替换该引用。

`createMesh(graphics)` 创建 Graphics 拥有的网格并返回调用方；`upload(graphics, mesh)` 仅在
调用期间借用对象。表现对象不保存 GPU 指针，先销毁表现对象不影响网格；Graphics/mesh 先销毁
不影响 CPU 表现对象，但调用方不得再传入失效对象。调用方在关闭 Graphics 前 releaseMesh，
成功后删除 CPU facade。更新失败应停止绘制直到后续成功，后端失败不承诺 GPU 回滚。

三角 UV 按拓扑保留，绳索当前 UV 为零。宿主窗口测试已用软件 Vulkan 和引擎像素读回验证
三角网格/管段的上传与移动显示，以及网格先释放后的恢复：保持已分离 facade 存活时，
upload 返回失败且不推进时钟；删除 facade 后，同一表现对象可创建新网格并继续上传显示。
测试也覆盖表现对象先销毁、网格继续绘制。不得向接口传入已经删除的 facade 引用；
Graphics owner 先销毁的单独验证见下文；这些结果不代表独显性能或跨平台验收。

### 粒子重放

`ParticleBakePresentation::prepare` 创建独立播放器和复用的 CPU sprite 缓冲。
`draw` 每次借用一个 `DrawItem2D` 材质模板，轨迹覆盖几何、颜色、UV 和稳定身份顺序，
保留调用者的纹理、shader、blend、layer、camera、canvas 和光照策略。绘制走现有 RenderSystem。
模板引用不会跨调用保留；图形资源必须存活到对应 Graphics 帧完成。采样失败不提交该批次。
该适配器不创建发射器、不运行模拟、不隐式推进播放时间。返回值是提交数量，不是 GPU 完成证明。

多源片段可使用按会话身份严格排序的 `ParticleBakeMaterial` 数组绑定不同材质。
调用期间借用 DrawItem2D 模板，资源仍须存活到 Graphics 帧完成；不在 adapter 中持久保存引用。
每个可见发射器必须有绑定，重复、无序或缺失绑定在任何 GPU 提交前失败；
允许为当前不可见的发射器保留额外绑定。模板的 layer/blend/texture 等策略分别生效。
查找为每可见粒子二分搜索，禁止每帧字符串模块查询。单材质入口保留统一模板行为。


## 完整模块的无实时物理图形配置

`physics-bake-render` 以客户端 minimal 选择加烘焙表现模块、font/devtools，保留完整宿主依赖，
当前共 58 模块。显式拒绝实时物理/粒子模拟与录制卫星，以及 Box2D/box3d/Jolt 第三方组。
Linux OBJECT（完整对象链接、无 section GC）与 SHARED 构建均通过；两者各五个进程隔离测试通过原生变形、
脚本变形、脚本粒子批处理和刚体目标提交。窗口断言来自软件 Vulkan 的引擎像素读回。

实际编译清单的 physics 源仅属于 bake 子树，无 particles 模拟源；该配置是 L1/L2/L3/L4
组合证据。这里的 58 模块不是最小渲染器承诺，font/devtools 是客户端直接符号引用所需。
其他平台与独显性能尚未由本项验收。SHARED 的动态引擎库来自同一裁剪目录，未加载完整主机领域库。运行命令见
[profile 说明](../../../test/profile/physics_bake/README.md#client-rendering-without-live-simulation)。


### Graphics 所有者退役与替换

`deformPresentationSurvivesGraphicsOwnerReplacement` 对三角布料与绳管分别验证：创建真实
headless GPU 网格并上传，销毁拥有网格的 Graphics，继续在 CPU 上 seek/sample，再创建新的
Graphics 并通过同一 presentation 创建/上传新网格。旧 facade 从未在 owner 销毁后再次使用。
Linux OBJECT/SHARED 两种裁剪构建均通过；这是顺序替换，不保证多个 Graphics 并发使用。

测试发现并修复 Vulkan 预热结果只能被首次 owner 消费后拒绝创建新 owner 的问题。
预热仍只启动一次；后续 owner 在锁外创建并独占自己的 Vulkan instance，不复用已销毁句柄。
Khronos 验证层在本机不可用，本轮没有验证层无错误结论。材质/纹理等其他借用资源的组合
生命周期仍需各自遵守并验证其合同。

### 粒子材质的销毁与替换

脚本粒子表现回归还覆盖调用方 ECS 材质的替换：完成上一帧并释放脚本别名后，
销毁两个 Renderable2D 材质，保持播放器存活；下一次绘制传入新材质，以相反的
层级遮挡关系通过引擎像素读回验证绑定更新。播放器随后先销毁，新材质仍可操作。
这符合逐次借用合同，不允许对已删除对象的脚本别名继续调用，也不代表任意
纹理、着色器、相机或 canvas 可以在 GPU 使用期间释放。该回归使用无自定义纹理材质，
底层图形资源的独立退役仍需单独验证。

纹理替换回归进一步绑定实际 1×1 GPU 纹理：同一粒子播放器依次绘制白、黑、白纹理，
检查绿色粒子变黑再恢复。每次先清除 Renderable2D 的借用绑定，再调用
`Graphics::releaseTexture`（等待在途 GPU 使用结束），确认 facade 已分离后删除，
最后绑定新纹理。纹理不作为 VM 对象暴露，因此没有残留脚本纹理别名。
此路径验证显式退役后的重新绑定，不支持在仍被材质引用时直接删除纹理。
不从该用例推导自定义 shader、camera、canvas 或其他图形后端的生命周期结论。
