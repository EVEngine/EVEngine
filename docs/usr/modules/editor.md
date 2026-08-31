# 编辑器构件模块

**脚本入口：** `eve.Editor()`

引擎**不附带**完整 3D 场景编辑器或 2D 地图编辑器，而是提供组装自定义编辑器所需的构件。新的工具协议不在核心中枚举“瓦片笔刷、地形隆起、摆放单位”等工具类型；任何实现 `IEditorTool` 的代码都能进入同一个会话。

> C++ 新代码应直接使用 `editing/*` 公共契约和对应的 `<domain>_editing/*` 卫星模块。
> `editor/Editor*Target.h`、`Editor*Graph.h`、`Editor*Runtime.h` 仅是源兼容 facade；权威实现、
> snapshot、validator、runtime publication 和 preview adapter 均由领域卫星拥有。发布用的
> `runtime-3d` profile 不链接 `editor`、`editing` 或任何 editing 卫星。

推荐把编辑器拆成五类可替换组件：

| 协议 | 职责 |
|------|------|
| `IEditorTool` | 生命周期、输入与手势；原生或 Squirrel 工具一视同仁 |
| `IEditableTarget` + capability | 被编辑对象；工具只查询自己需要的能力 |
| `IBrushKernel` / `IFieldBrushOperation` | 笔刷形状、衰减和“画什么”分别组合 |
| `IEditCommand` / `IEditConstraint` | 通用撤销事务与项目美术/玩法限制 |
| `IEditorOverlay` / `IEditorInspector` | 与渲染器和 UI 框架无关的预览、属性呈现 |

因此红警 2 / 帝国时代 2 风格的格子地图、魔兽 3 风格的 3D 地图和连续高度场不需要三套会话。它们分别提供目标 capability、工具和视口坐标转换即可。

设计参考了 Three.js `TransformControls`、Babylon.js `GizmoManager`、ImGuizmo、Unity `GridBrushBase` 与 Godot 编辑器插件的常见 API 形态；详见[编辑器模块设计](../../dev/编辑器模块设计.md)。

## 基本用法

```squirrel
local editor = eve.Editor();

// 3D 操作框
local gizmo = editor.newGizmo();
gizmo.setMode("translate");   // translate | rotate | scale | bound
gizmo.setSpace("world");      // local | world
gizmo.setPosition(0, 1, 0);
gizmo.setSize(1.0);

// 每帧：用相机射线拾取并拖拽
local axis = gizmo.pick(rayOx, rayOy, rayOz, rayDx, rayDy, rayDz);
if (axis != "" && mouseDown) {
    gizmo.beginDrag(axis, rayOx, rayOy, rayOz, rayDx, rayDy, rayDz);
}
if (gizmo.isDragging()) {
    gizmo.updateDrag(rayOx, rayOy, rayOz, rayDx, rayDy, rayDz);
    // 把 gizmo.getPosition* / getRotation* / getScale* 写回场景节点
}
if (mouseUp) gizmo.endDrag();

// 绘制：遍历 getPart* 用调试线 / 网格画出轴、环、平面
for (local i = 0; i < gizmo.getPartCount(); i++) {
    local kind = gizmo.getPartKind(i); // axis | plane | ring | box | center | handle
    // ...
}
```

```squirrel
// 项目组合一个可撤销的高度场工具；C++ 不认识“地形编辑器”窗口。
local target = editor.newHeightmapTarget("terrain", hm);
local falloff = editor.newSmoothBrushFalloff();
local kernel = editor.newCircleBrushKernel();
kernel.setSmoothFalloff(falloff);

local operation = editor.newAddScalarFieldOperation();
local sculpt = editor.newFieldBrushTool("terrain-sculpt", "Sculpt");
sculpt.setCircleKernel(kernel);
sculpt.setAddScalarOperation(operation);
sculpt.setRadius(3.0);
sculpt.setStrength(0.15); // 负值降低地形

local session = editor.newSession();
session.addFieldTool(sculpt);
session.bindHeightmapTarget(target);
session.activateTool("terrain-sculpt");

// 视口把鼠标射线换算为高度图坐标；一次 Down..Up 自动合并成一条事务。
session.dispatchPointer(0, 0, 0, cellX, cellY, 0.0, 0.0, 1.0);
session.dispatchPointer(2, 0, 0, cellX, cellY, 0.0, 0.0, 1.0);
session.undo();
session.redo();

// HeightmapTarget 直接包装同一个 hm；revision 变化时原地刷新 GPU 网格。
local mesh = editor.newHeightmapMeshSmooth(hm, 0.5, 3.2);
if (target.getRevision() != lastRevision)
    editor.updateHeightmapMeshSmooth(mesh, gfx, hm, 0.5, 3.2);
```

Target、kernel、falloff、operation 与 tool 之间是非拥有关系，项目需把它们保存在持久状态中。
`applyHeightmapBrush` 仍作为简单脚本的兼容入口，但定制编辑器应使用上面的组件和统一事务栈。

## 接口式工具会话（C++）

```cpp
EditorSession session;
TileBufferTarget target("ground", &tiles);
ConstantBrushFalloff hardEdge;
CircleBrushKernel circle(&hardEdge);
PaintIntFieldOperation paintGrass(17);
FieldBrushTool paint("paint-grass", "Paint Grass", &circle, &paintGrass);

session.bindTarget(&target);
session.addTool(&paint);              // 接受任意 IEditorTool
session.activateTool("paint-grass");

EditorPointerEvent down;
down.phase = EditorPointerEvent::Phase::Down;
down.x = tileX;
down.y = tileY;
session.dispatchPointer(down);        // 自动开启一次可撤销 stroke
```

将 `TileBufferTarget` 换成 `HeightmapTarget`、把操作换成 `AddScalarFieldOperation`，同一个 `FieldBrushTool` 就成为带衰减的地形升降笔刷。项目也可以实现新的 `IEditableTarget` capability（对象放置、道路、区域、体素等）以及对应操作；无需修改 `EditorSession`。

脚本可把同一个整数字段工具直接绑定到运行中的 `map.TileLayer`，修改会立即进入正常地图渲染、寻路与 FOV revision 流程：

```squirrel
local layer = eve.Map().newLayer(64, 64, 32, 32);
local target = editor.newTileLayerTarget("ground", layer);
local paint = editor.newPaintIntFieldOperation(17);
local hard = editor.newConstantBrushFalloff();
local circle = editor.newCircleBrushKernel();
circle.setConstantFalloff(hard);
local tool = editor.newFieldBrushTool("paint-grass", "Paint Grass");
tool.setCircleKernel(circle);
tool.setPaintIntOperation(paint);
session.addFieldTool(tool);
session.bindTileLayerTarget(target);
```

### 三维稀疏体积

Voxel 不会被伪装成二维 tile。`IIntVolumeTarget`、球/盒 Kernel、体积操作和
`dispatchPointer3D` 构成独立的三维 capability，但仍复用 `IEditorTool` 生命周期、
constraint 与 stroke transaction：

```squirrel
local world = eve.Voxel().newWorld();
local target = editor.newVoxelWorldTarget("terrain.voxels", world);
local hard = editor.newConstantBrushFalloff();
local sphere = editor.newSphereVolumeBrushKernel();
sphere.setConstantFalloff(hard);
local paint = editor.newPaintIntVolumeOperation(7); // 0 为擦除
local tool = editor.newVolumeBrushTool("voxel.paint", "Paint Voxels");
tool.setSphereKernel(sphere);
tool.setPaintIntOperation(paint);
tool.setRadius(2.5);
session.addVolumeTool(tool);
session.bindVoxelWorldTarget(target);
session.activateTool("voxel.paint");

// 视口 raycast 决定三维目标坐标，再转发 Down / Move / Up。
session.dispatchPointer3D(0, 0, 0, vx, vy, vz, 0, 0, 0, 1.0);
session.dispatchPointer3D(2, 0, 0, vx, vy, vz, 0, 0, 0, 1.0);
```

`VoxelWorldTarget.getRevision()` 读取 live world 的单调 revision，因此游戏逻辑、流式加载或
其他脚本产生的变化也能使预览与保存票据失效。`getDirtyMinX/Y/Z`、`getDirtyMaxX/Y/Z`
提供局部 remesh/overlay 范围；`clearDirtyVolume` 在消费者完成刷新后清除它。

### 项目限制

实现 `IEditConstraint::evaluate()` 并注册到 `session.constraints()`。约束可以允许、给出警告或拒绝任意 `IEditCommand`，例如锁定水岸坡度、限定可用 tile、吸附建筑朝向、阻止穿过地图边界。所有命令都经 `EditorContext::execute()` 进入约束和事务，拒绝的命令不会污染撤销栈。

### 自定义呈现

视口实现 `IEditorOverlay`，Inspector 实现 `IEditorInspector`。工具只输出圆、线、矩形、文本和属性意图，所以可同时接入 2.5D 正交视口、3D 透视视口、ImGui 或游戏自己的 UI。

## Squirrel 自定义工具

脚本工具也实现相同的 `IEditorTool` 协议。回调返回位标志：`1` 表示已处理，`2` 表示捕获指针，`4` 表示释放指针。

```squirrel
local editor = eve.Editor();
local session = editor.newSession();
local road = editor.newScriptTool("road", "Road");

road.setActivateCallback(function() { previewRoad(); });
road.setPointerCallback(function(phase, pointerId, button, x, y, dx, dy,
                                 pressure, shift, control, alt) {
    if (phase == 0) { beginRoad(x, y); return 1 | 2; } // Down
    if (phase == 1) { updateRoad(x, y); return 1; }    // Move
    if (phase == 2) { finishRoad(); return 1 | 4; }    // Up
    cancelRoad(); return 1 | 4;
});

session.addTool(road);
session.activateTool("road");
// 视口负责把屏幕/射线坐标变换到工具坐标后转发。
session.dispatchPointer(0, 0, 0, mapX, mapY, 0.0, 0.0, 1.0);
```

`EditorSession` 和工具都是非拥有关系；脚本必须像上例一样持有 `road`，直到从会话移除。

### 注册并执行项目命令

项目可用 `registerScriptCommand(id, name, category, callback)` 把游戏专用操作注入统一命令
服务，退出插件或编辑器时用 `unregisterScriptCommand(id)` 清理。会话通过
`getCommandCount()` 与 `getCommandId/Name/Category(index)` 枚举命令；
`planCommand(id, payload)` 只生成并保留计划，随后用 `executePlan(planId, context)` 执行；
不需要预览时可直接 `executeCommand(id, payload)`。这些入口和 C++ 命令服务共享约束、
事务及 HostProfile 策略，因此游戏内建造玩法和开发编辑器可以复用同一条命令路径。

## 地图笔刷

旧的 `Brush` / `EditorHistory` API 为兼容已有脚本保留。新编辑器优先使用上面的协议式会话；旧 API 适合很小的纯 tile 工具。

## 材质文档（C++）

`MaterialDocumentTarget` 把 `graphics::Material` 的核心创作参数暴露成 UI 无关的属性模型，
包括 shading model、tint、metallic/roughness、纹理/Shader 资产引用、surface/blend、
alpha cutoff、parallax、光照和阴影开关。Presenter 可直接使用它的 `IPropertyProvider`，
所有赋值仍生成 `DomainOperation` 并通过统一事务执行：

```cpp
MaterialDocumentTarget material("materials/metal-panel");
SelectionSnapshot selection = /* one Asset selection for this target */;
auto operation = material.makeSet(selection, PropertyPath("shading.metallic"),
                                  EditorValue(0.8), PropertySetMode::Absolute);
```

`snapshotValue()` 返回可交给 `DocumentService` 保存的确定性数据；`loadSnapshot()` 验证版本、
属性名、类型、枚举和数值范围后才原子替换内容。`validate()` 额外报告跨字段问题，例如
custom shading 未指定 Shader、masked surface 未指定含 alpha 的 albedo，以及高度图存在但
parallax scale 为零。实际 GPU `Texture*` / `Shader*` 的解析和预览场景由 graphics host 负责，
文档本身只保存稳定资产引用。

材质图的规范实现位于 `material_editing/MaterialGraph.h`，通用图和后台任务契约分别位于
`editing/EditingGraph.h`、`editing/EditingTaskService.h`。`MaterialGraphDomain` 负责 typed-pin
校验和确定性编译；`MaterialEditorService` 只允许成功且 revision 匹配的产物替换预览。
`editor/EditorGraph.h` 与 `editor/EditorTaskService.h` 仅保留旧 include/namespace 兼容入口。

## 动画状态图（C++）

`AnimationStateGraphDomain` 在通用 `GraphDocument` 上定义 `animation.state` 领域。State 节点
保存稳定 Clip 资产引用、播放速度和循环设置；Transition 节点保存混合时间、退出时间以及
float/bool/trigger 条件。连接采用 `state -> transition -> state`，从结构上允许同一状态拥有
多个独立过渡，同时避免把运行时 `AnimClip*` 写入文档。

```cpp
AnimationStateGraphDomain domain;
auto idle = domain.makeStateNode(GraphNodeId("idle"), "asset://anim/idle.eva");
auto run = domain.makeStateNode(GraphNodeId("run"), "asset://anim/run.eva");
auto edge = domain.makeTransitionNode(GraphNodeId("idle-to-run"));
// 将 idle.out -> edge.in、edge.out -> run.in 写入 GraphDocument，
// 并把 graph.parameters.entry 设为 "idle"。
auto result = domain.compile(graph.snapshot(domain.domain()));
```

编译会检查 Schema 版本、入口、Clip 引用、节点属性、过渡完整性、条件运算符和重复来源/
目的地。启用 `animation` 模块时，`AnimationStateGraphRuntimeBuilder` 可通过 host 提供的
Clip resolver 把同一份快照构造成真正的 `animation::AnimStateMachine`；无法解析的资产以
结构化 `NotFound` 返回，编辑器模块不持有资源指针。

## UI 文档（C++）

规范 API 位于 `ui_editing/UiDocument.h`，包含 hierarchy/layout/style/content authoring、版本化
snapshot、确定性 preview/picking 和可选 `UIHost` publication；`editor/EditorUiDocumentTarget.h`
只保留旧 include/namespace 兼容入口。

`UiDocumentTarget` 是 retained UI 的可持久化创作模型。每个 Widget 使用稳定 ID，并保存父级、
类型、名称、文本、visible/enabled，以及 position、size、anchor 和 pivot。内容皮肤还保存字体资产、
字号、文本色、双轴对齐、纹理资产、`stretch`/`contain`/`cover` 适配方式和祖先裁剪开关。Hierarchy 工具通过
`IUiDocumentEditTarget` 创建、删除、重命名和换父节点；Canvas 工具通过 `makeSetLayout()`
生成同一类可逆事务。父节点不存在、循环层级、删除非叶节点以及越界 Anchor/Pivot 都会在
计划或应用阶段拒绝。

该 target 同时实现 `IPropertyProvider`。通用 Inspector 可对多选 Widget 编辑名称、文本、
可见性、启用状态和布局字段，并正确显示 `Mixed` 值。多选操作先验证完整候选树再一次性提交，
不会部分修改文档。`snapshotValue()` / `loadSnapshot()` 提供确定性、版本化且原子校验的保存
边界；schema 2 会写入内容皮肤，同时继续读取无皮肤字段的 schema 1 文档。

`UiSkinPreviewPlanner` 使用 `IUiSkinAssetResolver` 校验字体及纹理尺寸，并把当前 revision 的布局
快照转换成确定性的文字/图片 draw plan。Plan 已计算 padding、contain 尺寸、cover UV 裁切和父级
裁剪矩形；缺失资产返回结构化诊断。`UiOffscreenPreviewRenderer` 可注入
`IUiSkinPlanRenderer`，在原有 box/tint 背景之后绘制同一份 plan，因此 UI host 或 graphics adapter
无需重新解释文档语义。

## 流体表面与湿润材质（C++）

`FluidSimulationTarget` 负责体积粒子求解器的容量、核半径、密度、黏性、PBF 迭代和预算。
`SurfaceFluidTarget` 则覆盖原先无法编辑的表面表现链路：液滴摩擦、黏附与脱离、接触角和合并半径，
wet-film 扩散、蒸发和饱和值，以及速度拉伸、宽高比、表面偏移、干湿粗糙度/高光、变暗和法线强度。

所有字段均通过 `IPropertyProvider` 生成可撤销事务；非法接触角、负速率、越界材质值和非有限数会在
提交前拒绝，反常的干湿粗糙度或高光顺序以 Warning 呈现。`snapshotValue()` / `loadSnapshot()`
提供原子保存边界。启用 `fluids` 时，`SurfaceFluidRuntimeApplier` 一次发布到真实
`SurfaceDropletSimulation`、`SurfaceFluidRenderParams` 和 `SurfaceWetnessParams`，避免 Inspector
只更新其中一段而造成预览与运行时不一致。

`SurfaceFluidPreviewService` 接收三角面拓扑、UV 和以 triangle+barycentric 表示的稳定液滴种子。
每次时间轴拖动都从这些初始数据重建独立 Simulation，再以固定 Step 重放到目标时间；因此先看
0.8 秒、回拖到 0.2 秒、再前进到 0.8 秒会得到逐值相同的液滴和湿润度结果。Snapshot 输出
世界空间 position/normal/major/minor axis、cap height、液滴 wetness 和逐顶点 wetness，宿主无需访问
solver 私有状态。请求同时限制 600 秒、最大 Step、顶点和液滴数量，并拒绝旧 document revision。
`SurfaceFluidOffscreenPreviewService` 将同一 Snapshot 接入共享 Canvas/readback；具体正交或透视相机
构图由 `ISurfaceFluidPreviewRenderer` 决定。

## Stylize Recipe（C++）

`StylizeRecipeTarget` 把现有 `StyleDefinition` / `StyleParameterDesc` 元数据转成稳定 Pass 文档。
每个 Pass 保存 ID、Style、Enabled、Priority 和显式参数 Override；创建、删除、排序、启停、参数修改与
Reset 都生成可逆事务。Inspector Schema 直接读取引擎参数表的 Default/Min/Max，同 Style 多选支持
Mixed 值；未知 Style、越界 Override、重复 ID、超过 64 Pass、混合 Pipeline Stage 或把 mesh-only
Style 放进 post recipe 都会在提交或原子快照加载时拒绝。

`StyleInstance` 新增可恢复的 authored priority override，使 Recipe 的可视顺序与真实编译顺序一致。
`StylizeRecipeRuntime` 先创建全部 Instance、应用 Override 并编译完整候选 Recipe，成功后才替换当前
generation；应用时必须匹配 document revision。`StylizeOffscreenPreviewService` 使用独立候选 runtime
接入共享 Canvas/readback，不修改游戏正在使用的 StyleRecipe。全部 Pass 禁用时会执行无效果拷贝，
因此 Editor 仍能生成对照预览。

## Decal 文档与投影预览（C++）

`DecalDocumentTarget` 用稳定文档 ID 取代运行时临时整数 ID。Inspector 可编辑世界位置、投影法线、
Yaw、Size/Depth、Kind、Albedo/Normal/Params 纹理、Atlas UV、四个材质通道强度、Blend，以及
Lifetime/Fade。零法线、越界 UV、未知 Blend、空 Kind 和非法数值会在事务提交前拒绝；缺少 Albedo、
无效的 Additive/Emissive 组合和 Persistent/Fade 组合以 Warning 呈现。快照加载先验证完整候选值，
失败不会修改当前文档。

`DecalGizmoPreviewService` 输出带 revision 的 oriented projection box 和 surface-normal arrow。
`DecalRuntimeBinding` 会先解析全部纹理，再构造完整 `DecalInstance`；`DecalManager::replace()` 在注册表
副本中移除旧 generation、执行 Kind quota 并插入新 generation，最后一次 swap。因此纹理缺失、旧 ID
过期或候选非法时，场景中的旧贴花保持不变，同一机制也适用于 Undo/Redo 发布。

## Physics Collider 与 Joint（C++）

`PhysicsColliderTarget` 为 Box2D/Box3D 提供同一套后端无关创作属性：body type、shape kind、
offset/size/radius/capsule height、density/friction/restitution、sensor、category/mask 和复杂形状
资产引用。二维和三维 Schema 会分别限制可用形状及 Vec2/Vec3 维度。属性修改可撤销，快照记录
维度并拒绝把 3D Collider 载入 2D 文档。`validate()` 会报告缺少 mesh/height-field/polygon
资产、无效 capsule 比例和 dynamic + zero density 等组合问题。

启用 `physics` 时，`PhysicsColliderRuntimeBuilder` 可把 primitive 文档直接创建为真实的 Box2D
`Fixture` 或 Box3D `Shape3D`，并应用 offset、材质、sensor 和过滤位。Polygon/Chain、Convex Hull、
Triangle Mesh 与 Height Field 需要项目的资产解析器，因此 bridge 会明确返回 `Unsupported`，
不会静默退化成 Box。

`PhysicsJointTarget` 保存稳定 Body 引用、distance/revolute/prismatic/spherical/wheel 类型、两个
锚点、轴、Limit、Motor、connected collision 以及 Break force/torque。Inspector 可在创建运行时
Joint 前发现相同/缺失 Body、零轴和反向 Limit 范围。

## Audio Source 与 Mixer（C++）

`AudioSourceTarget` 保存稳定 Clip 资产引用、static/stream 模式、autoplay/loop、volume/pitch、
loop range、position/velocity/direction、listener-relative、reference/max attenuation distance 和
Mixer Bus 路由。它实现通用属性 Schema、可逆事务和版本化快照；`validate()` 会报告缺失 Clip、
无效循环范围、反向衰减距离、零方向和空 Bus。空间参数可直接驱动视口中的声源点、方向箭头及
内外衰减球 Gizmo。

启用 `audio` 时，`AudioSourceRuntimeApplier` 把已验证设置应用到现有 live `audio::Source`。
Clip 解码和 Source 创建仍由 audio/sound host 负责；Editor 文档不保存 OpenAL handle。当前运行时
只有整段 Loop，因此 loop-start/end 作为创作元数据保留，供后续区间播放 backend 或导出器使用。

`AudioMixerTarget` 提供以不可删除 `master` 为根的 Bus 树。每个 Bus 保存 volume、mute、solo 和
结构化 effect chain，可创建、删除叶节点、换父节点或修改设置，并完整 undo/redo。快照加载会
原子检查重复 ID、缺失 Parent、路由循环以及未最终汇入 Master 的孤立树。

## Particle Graph（C++）

规范 API 位于 `particles_editing/ParticleGraph.h`；`editor/EditorParticleGraph.h` 仅作为旧 include/namespace
兼容入口。通用图文档契约位于 `editing/EditingGraph.h`。

`ParticleGraphDomain` 在通用 `GraphDocument` 上定义 `particles.emitter` 领域。模块链从唯一的
Emission 开始，经过可选 Motion/Collision，进入唯一 Renderer，最后终止于唯一 Output。
节点属性对应现有 `ParticleEmitter::Config`：发射率、生命周期、区域和随机种子；方向、速度、
重力、阻尼和 simulation space；碰撞模式；render/material/sort/GPU/soft-particle；以及 buffer、
quality、priority、overflow 和 per-frame spawn cap。

编译器拒绝分支、合流、循环、悬空 Pin、错误方向、重复必需模块以及越界属性，并输出
renderer/backend 无关的配置对象。`preview()` 使用固定步长、粒子总预算和每帧 Spawn 预算生成
确定性的 spawned/peak-live/dropped 估算；结果携带源文档 revision，预算或 buffer 饱和以结构化
Warning 返回，因此 host 不会发布过期或无界预览。

启用 `particles` 时，`ParticleGraphRuntimeBuilder` 可把已编译配置应用到真实
`ParticleEmitter`。它会在任何 setter 前检查 Buffer Size 并解析所有 Texture 资产，资源失败时
不会部分修改 live emitter；Buffer 不匹配要求 host 按 Output 容量重建 emitter。

`ParticleEmitterOffscreenPresenter` 提供无需项目自写 callback 的真实外观预览。它创建临时
Emitter，解析纹理，强制关闭随机种子与 GPU simulation，按整数 Tick 和固定步长运行到精确 scrub
时间，再通过 `ParticleRenderSystem::renderEmitter()` 只绘制该实例。这个入口复用正式运行时的
Sprite、Ribbon、Flipbook、排序和材质提交逻辑，但不会遍历全局 ECS，因此不会把游戏场景里的其他
Emitter 混入预览。无论应用、步进或绘制失败，临时实体都会被释放。单次预览限制为 600 秒或
一百万个 Step，防止异常请求阻塞 Editor。

### Dialogue Graph 与本地化 QA

`DialogueGraphDomain` 提供 `dialogue.conversation` 图域。Line、Choice、Branch、Call、Command、
Wait、End 是业务节点；Choice/Branch 的每个出口使用带稳定 ID 的 Route 节点保存选项文本或条件，
避免把易丢失的业务数据塞进通用 Edge。编译阶段检查入口、Pin、路由结构、节点字段和参数，启用
`dialogue` 时可由 `DialogueGraphRuntimeBuilder` 无损生成并再次验证 `ConversationDocument`。

`LocalizationDocument` 按稳定 key 保存 source/context 与各 locale 的翻译、voice asset、制作状态和
实测时长。`analyze()` 汇总 translated/voiced/approved 覆盖率，并报告缺译、占位符不一致、缺语音、
孤立时长等结构化诊断；snapshot/load 是确定且原子的。启用 `dialogue` 与 `audio` 时，
`LocalizationVoiceAudition` 会先解析真实 Source，再交给 `DialogueVoice` 播放，解析失败不会改变文档。

### Definition/Schema 资产

`DefinitionDocument` 保存稳定 type/id、schema version、JSON payload，以及由 schema presenter 提取的
引用路径。宿主可注入 schema validator 和 definition resolver，统一得到字段约束及缺失跨引用诊断；
snapshot/load 保证失败不污染当前文档。启用 `definitions` 时，`DefinitionRuntimePublisher` 通过真实
`DefinitionRegistry` 执行 insert/replace，保留其版本校验、generation 与 stale-handle 语义。

### Live Scene 与 Prefab

启用 `scene` 时，`SceneHostEditorTarget` 会导入真实 retained SceneHost，并把通用 hierarchy/TRS
operation 作为增量 mutation 提交。新增、叶节点删除、改名、重挂和 TRS 不会 remount 整棵树，已有
render/physics/audio link 因而保留；提交前会比较 mirror 与 live host，检测到游戏侧外部修改时返回
Conflict，不会静默覆盖。

`ScenePrefabService` 从任意 scene target 捕获一个子树，生成稳定 source-id 的 prefab snapshot。
实例化计划把 source id 映射为 `<instance>/<source>`，可与普通 transaction/undo 共用；
`applyOverrides()` 产出递增 revision 的新 prefab 值，`revertOverrides()` 产出可逆 rename/reparent/TRS
operation。结构缺失、跨实例重挂、ID 冲突、父节点缺失和 prefab cycle 都会在修改前被拒绝。

模块可实现 `ISceneComponentPayloadProvider`，把自己拥有的组件字段注册到
`SceneComponentPayloadRegistry`。Scene target 通过
`ISceneComponentPayloadTarget::editorCapabilityId()` 暴露聚合入口：Inspector 先按场景对象枚举
`SceneComponentPayloadRef`，再用 `makeSceneComponentSelection()` 获得通用属性选择并解析
`IPropertyProvider` 与对应的 `IDomainOperationTarget`。引用同时携带 generation/revision；组件被替换或外部
修改后，`validatePayload()` 会返回 NotFound/Conflict，而不是让旧 Inspector 覆盖新数据。组件适配器若同时
实现 `IDomainOperationTargetStaging`，其字段修改即可直接使用统一 transaction、undo/redo 和 Schema 校验。

对已有的模块编辑 Target，不需要再手写一层字段代理。`SceneComponentPropertyBindings` 可把一个
`SceneComponentPayloadRef` 绑定到模块自己的 selection、`IPropertyProvider` 和
`IDomainOperationTarget`，并在每次枚举时读取操作 Target 的最新 revision。目前
`AudioSourceTarget`、`PhysicsColliderTarget`、`PhysicsJointTarget` 和 `MaterialDocumentTarget` 都提供
原子 staging，因此经该适配器进入 Scene Inspector 后仍能完整提交及撤销。Bindings 与底层 Target 均为
非拥有关系，宿主应保证它们在 Registry 和 Scene Target 之后析构。

需要立即写回实时声音时，用 `AudioSourcePublishingTarget` 代替直接提交 `AudioSourceTarget`：属性仍从
`authoringTarget()` 读取和生成 operation，但 transaction 的 operation target 指向 publishing target。
它会先在 detached candidate 上应用整笔变更，再通过 `IAudioSourceRuntimeSink` 发布；sink 拒绝时作者状态与
revision 均不改变。Undo/redo 也走相同 candidate-first 路径。引擎提供的 `AudioSourceRuntimeSink` 将该协议
连接到真实 `audio::Source`。Sink 与 Source 都是非拥有引用，宿主销毁 Source 前必须先解除 SceneLink、
component binding 和 publishing target。

Material 使用同样的原子路径：`MaterialPublishingTarget::authoringTarget()` 负责 Inspector Schema 与
operation，`IMaterialRuntimeSink` 负责整份 candidate 的无部分写入发布。启用 `graphics` 时，
`Renderable3DMaterialRuntimeSink` 会先解析 albedo/normal/height texture 与 shader 的全部资产引用，再更新
Renderable 的 tint、metallic、roughness、parallax、lighting 和 shadow 参数。内置 legacy Renderable sink
只接受 PBR、opaque、alpha、single-sided 组合；其他 surface/shading 模式返回 Unsupported，项目应注册
能够原子发布其专用 Material pipeline 的 sink。

3D Collider 用 `PhysicsColliderPublishingTarget` 与 `PhysicsCollider3DRuntimeSink` 接入同一流程。Sink 会先
校验完整 candidate 和 static mesh/height-field 约束，必要时预切换 Body type，然后通过
`PhysicsColliderRuntimeBuilder` 创建并完整配置 replacement；只有成功后才对旧 `Shape3D` 调用
`destroy()` 并更新 generation-qualified shape 槽。Builder 或资产解析失败会恢复原 Body type，旧 shape、
作者文档和 revision 均保持不变。Undo 会创建另一代 shape，而不会尝试复活已经失效的 runtime handle。
从 complex static collider 同时切换为 non-static primitive 时，应拆成两笔提交：先换 primitive，再改 Body
type，避免 Box3D 在旧 complex shape 仍存活时拒绝类型转换。

2D 使用同一 `PhysicsColliderPublishingTarget`，将 dimensions 设为 `2` 并绑定
`PhysicsCollider2DRuntimeSink`。AssetDB 的 polygon metadata 使用 3–8 个有限、严格凸的 packed XY
顶点；chain 使用至少两个顶点，并可用 `loop: true` 闭合。Resolver 会在触碰 Box2D 前拒绝凹多边形、
连续重复点、超预算数据和 kind 不匹配；builder 负责像素到米换算并应用 authored offset、density、
friction、restitution、sensor 与 collision filter。Fixture 同样先创建成功再销毁旧实例，undo 不复用失效
指针。

### Skeleton Viewport Overlay

`SkeletonOverlayBuilder` 把不可变 `SkeletonOverlayBone` 列表转换为通用 `EditorGizmoSnapshot`，输出 joint、
bone-line、选中骨骼的 XYZ axis，以及可选 yaw/pitch constraint-arc。Retarget 的 matched、unmatched、
ambiguous 状态使用稳定配色，未匹配骨骼使用虚线；blend mask weight 会调节骨骼亮度。Builder 在生成
primitive 前检查稳定 ID、父级存在、层级环、有限 TRS、mask 范围、constraint min/max 和 maximumBones
预算，因此 renderer 不需要重新解释动画数据。

启用 `animation` 时，`AnimationSkeletonOverlayAdapter` 从真实 `AnimSkeleton` 和 `AnimPose` 提取 world
position/quaternion，并接受 selected bone、retarget mapping 与 mask。它只更新 Pose 的派生 world cache，不
修改 local pose。Vulkan、Web 或游戏内调试 HUD 只需实现 `joint`、`bone-line`、`axis` 和
`constraint-arc` primitive 的绘制。

### Material Preview、Light 与 Environment

`MaterialPreviewService` 把材质 snapshot、文档 revision、mesh/camera/environment 设置封装为不可变
`MaterialPreviewRenderRequest`。每次请求带唯一 isolated scene ID，renderer host 不需要访问 live scene；
只有成功且 revision 仍匹配的 artifact 才能 publish。自定义 mesh、分辨率、相机距离及材质交叉字段会在
调用 renderer 前校验。

`MaterialStudioController` 在这些底层契约上提供实时编辑会话。UI 在 slider、颜色选择器或资产槽开始交互时
调用 `beginInteraction()`，把中间值传给 `updateInteraction()`，并用宿主的单调毫秒时间调用 `tick()`。
中间值只进入 owned draft 和隔离预览；`commitInteraction()` 才把最终值作为一个事务发布，因此一次拖拽只
生成一条 undo 记录。`cancelInteraction()` 不修改 live material。默认预览间隔约 33ms，也可通过
`setPreviewRate()` 在 1–240 Hz 内调整。完整布局、状态、生命周期与失败契约见
[`实时材质 Studio 设计`](../../dev/2026-08-31-realtime-material-studio.md)。

`Light3DDocumentTarget` 和 `EnvironmentDocumentTarget` 实现标准 `IPropertyProvider`、可逆 operation、
snapshot/load 与结构化诊断。Light 覆盖 type、position/direction、HDR color/intensity、radius、shadow
及 volumetric；Environment 覆盖 static/DayNight/Weather mode、sky/ambient/exposure、时钟/大气、
天气/风/雷电/雾。对应 runtime applier 会先完整校验，再原子地写入真实 `Light3D`、`DayNight` 或
`Weather`，并拒绝 mode 不匹配及多环境 owner。

### Map Structure、Road 与 Placement

`MapDocumentTarget` 提供 `eve.editor.target.map-structure` capability，把 tile/object/road/group layer、
road spline 与 asset placement 纳入统一 revision、staging transaction 和 undo operation。Outliner 可稳定
排序、隐藏、锁定和改名 layer；锁定 layer 会在 planning 与 apply 两个边界拒绝内容修改，非空 layer
不能删除或改变 kind。

Road 使用稳定 point ID、XYZ 与逐点 width，支持 open/closed spline；`previewRoad()` 在 triangle budget
内输出 renderer-neutral strip vertices/indices、中心线长度和源 revision，并报告零长度 tangent、缺材质及
预算超限。Placement 保存稳定 asset ref 和完整 TRS，仅能写入 object layer。整个 map structure 可确定性
snapshot/load，缺层引用、重复 order、非法 scale 和错误 spline 结构会原子拒绝。

### Building Footprint 与可逆放置

启用 `building` 时，`BuildingPlacementTarget` 直接绑定真实 `PlacementWorld`。`preview()` 复用当前
building definition、snap mode、rotation mode、terrain/adjacency/occupancy rule，返回吸附坐标、规范化
旋转、每个 footprint cell 的 bounds/terrain/occupant 和稳定 rule diagnostic；预览不修改世界。

Place 使用 editor 指定的稳定正整数 instance ID；move/remove operation 的 inverse 保存完整 cell/world
pose、channel、properties、tags、garrison 及其 revision。`PlacementSystem::restoreExact()` 会重新执行当前
规则和占用检查后恢复原 ID，因此 Undo 遇到已被其他建筑占用的格子时返回 Conflict/Rejected，不会覆盖
新状态。成功恢复同步推进全局 ID allocator，后续正常放置不会重用该 ID。

```squirrel
local buf = editor.newTileBuffer(64, 64);
local brush = editor.newBrush();
brush.setTool("paint");  // paint | erase | fill | line | rect | stamp
brush.setTile(3);
brush.setSize(3);
brush.setShape("circle");
brush.paintAt(buf, tx, ty);

// 同步到 map.TileLayer（示例）
for (local y = 0; y < buf.getHeight(); y++)
    for (local x = 0; x < buf.getWidth(); x++)
        layer.setTile(x, y, buf.getGid(x, y));
```

## 编辑器壳

### 可组合 Workspace（推荐）

`EditorWorkspace` 是 UI 无关的组合模型，不提供固定编辑器窗口。项目注册任意面板描述，
再由 `ui`、游戏 HUD、MCP host 或其他 presenter 动态生成界面。Workspace 同时提供带通道的
Selection/Focus，因此开发编辑器和游戏内建造模式可以共享语义状态而使用不同布局。

```squirrel
local ws = editor.newWorkspace("level", "My Level Tools");
ws.setRegionSize("left", 240);
ws.registerPanel("outliner", "Outliner", "left", 10);
ws.registerPanel("viewport", "Scene", "center", 20);
ws.setPanelCapability("viewport", "scene.viewport.3d");
ws.layout(config.width, config.height);

// 枚举 descriptor 动态生成项目自己的 UI，而不是依赖固定 shell。
for (local i = 0; i < ws.getPanelCount(); ++i) {
    print(ws.getPanelId(i) + " -> " + ws.getPanelRegion(i) + "\n");
}

ws.select("world", "scene", "level-1", "tree-42", "vegetation.tree", false);
```

完整组合示例见 [`examples/composable-editor`](../../../examples/composable-editor)：项目脚本用
五个面板 builder 组合地形、材质、反射 MVVM、ECS 与游戏命令；C++ 不认识这些具体面板。

### 动作时间轴编辑器

`newActionTimelineEditor(targetId, timelineTable)` 把版本化的 `eve.action.timeline`
资产交给原生动作编辑器。原生对象是时间轴、命中测试、事务、撤销/重做和确定性预览游标的
唯一事实源；脚本只负责用项目自己的 UI 绘制 `getItem*` 布局并转发指针输入。

```squirrel
local created = editor.newActionTimelineEditor("ability.light-attack", timelineAsset);
if (!created.ok) throw created.status.summary;
local actionEditor = created.value; // ownership == "owned"

local ws = editor.newWorkspace("combat", "Combat Action Editor");
local composed = actionEditor.configureWorkspace(ws); // Assets/Preview/Inspector/Timeline
actionEditor.setViewport(900.0, 36.0, 145.0);

// 视口输入；一次 Down..Up 只生成一个撤销事务。
actionEditor.pointerDown(mouseX, mouseY, false);
actionEditor.pointerMove(mouseX);
actionEditor.pointerUp(mouseX);

actionEditor.play();
actionEditor.update(dt);
animationPlayer.setTime(actionEditor.getPreviewTime());
```

工厂与所有可能失败的编辑操作返回通用 Result 表：`ok`、`value`、`status.code`、
`status.summary` 和 `status.diagnostics`。返回的动作编辑器由 Squirrel VM release hook 拥有，
仅可在创建它的线程使用；`configureWorkspace` 不保留传入的 Workspace 指针，`snapshot`
返回与编辑器生命周期解耦的规范化资产值。完整可运行示例见
[`examples/combat-action-editor`](../../../examples/combat-action-editor)。

```squirrel
local dock = editor.newDock();
dock.setRegionSize("left", 200);
dock.layout(config.width, config.height);
// 用 dock.getRegionX/Y/W/H("center"|"left"|...) 放置 ui 窗口

local toolbar = editor.newToolbar();
toolbar.addTool("move", "Move");
toolbar.addTool("paint", "Paint");
toolbar.setShortcut("move", "W");

local insp = editor.newInspector();
insp.addChoice("mode", "Mode", "translate,rotate,scale", "translate");
insp.addFloat3("pos", "Position", 0, 0, 0);
```

## AI、队列与关系图

`CrowdDocumentTarget` 用稳定 ID 编辑 Agent、多边形 Zone 和 Waypoint Path；
`BehaviorGraphDomain` 在交给运行时前检查唯一根节点、环、多父节点、悬空边和不可达节点。
启用 `crowd` 时，运行时桥会把已验证文档发布为 named agents。

`RuntimeQueueInspector` 会立即复制 Orders 与 Production 的任务和事件，UI 不需要长期持有
运行时 borrowed pointer；它支持 owner/state/kind 筛选，并报告超时和非法进度。
`SocialDocumentTarget` 对 ownership、control、assignment 和加权 relation 提供可逆编辑、
原子快照与交叉引用校验，启用 `social` 时可发布到真实 `SocialGraph`。

音频编辑的规范 C++ API 已拆到独立的 `audio_editing` 模块，使用
`audio_editing/AudioTarget.h`、`audio_editing/AudioEffects.h`、
`audio_editing/AudioWaveform.h`、`audio_editing/AudioImportDiagnostics.h` 和
`audio_editing/AudioTransport.h`。原有 `editor/EditorAudio*.h` 仅作为兼容入口保留，
新代码不应再通过 `editor` 获取音频编辑契约。

Scene 和 Map 的规范 C++ authoring API 分别位于 `scene_editing/SceneTarget.h`、
`scene_editing/SceneComponentPayload.h` 与 `map_editing/MapDocument.h`。对应的
`editor/EditorScene*.h`、`editor/EditorMapDocument.h` 只用于源代码兼容。

Animation clip 的规范 authoring API 位于 `animation_editing/AnimationClip.h`；
`editor/EditorAnimationClip.h` 是兼容入口。

音频资源可通过 `AudioAuditionTransport` 做 play/pause/stop/seek 和区间循环试听。
所有操作都绑定资源 revision；检测到旧 revision 时会立即停止并解绑播放源，避免继续试听
已经被重新导入或覆盖的音频。启用 `audio` 时使用 `AudioSourceTransportBackend` 连接真实 Source。

复杂 3D Collider 使用 `AssetDatabasePhysicsColliderResolver` 从 AssetDB 元数据解析并限制
Convex Hull、Triangle Mesh 或 Height Field 的 CPU 几何；索引范围、顶点/三角形/采样预算、
有限数值和 Shape Kind 会在进入 Box3D 前检查。把 resolver 传给 `PhysicsColliderRuntimeBuilder`
即可创建真实复杂 Shape，同时继续应用 Offset、Sensor、Filter 和 Material 设置。

基础设施设置通过 `ProjectSettingsTarget` 编辑。默认 Schema 覆盖 Asset Root、Import Cache、
Unsigned Plugin、Network Endpoint、Auth Token 与 Database Connection；需要重启的修改会单独列出。
敏感项只接受 `secret://` 引用，读取时返回脱敏值，快照不会接受明文凭据。各 Importer 可用
`importerSettingsSchema` 提供自己的同类设置。

`PluginPermissionTarget` 对 filesystem/network/database/native-script/process 权限保存显式窄 Scope
的 allow/deny/ask 决策，拒绝 `/` 或 `*` 这类全局授权。`EditorEventTimeline` 则提供有容量上限的
事件副本、Correlation/Severity/Tick 筛选、Dropped 计数和 generation-safe 分页。

离屏预览通过三个窄 Graphics 接口完成 Canvas 分配、目标绑定与绘制。服务会在 DrawCallback
期间切换到目标 Canvas，并在成功、失败或异常时恢复此前目标，然后再做 CPU Readback。
`OffscreenMaterialPreviewRenderer` 将 MaterialPreviewService 请求接入该流程；
`ParticleOffscreenPreviewService` 先编译和预算检查粒子图，再交给隔离的真实 Emitter presenter；`UiOffscreenPreviewRenderer`
会把可见 Widget 的 Box Model 与 Tint 直接栅格化为 revision-safe Artifact。

`AudioImportDiagnosticsService` 对解码 PCM 做每声道 Peak/RMS/DC Offset/Clipping/Silence
分析，同时给出解码内存、压缩比和 Streaming 建议。`AudioEffectChainTarget` 支持 Gain、
Low/High Pass、Delay、Reverb、Compressor 的稳定实例、参数范围、Bypass、Dry/Wet、排序、
撤销和原子快照；链可赋给 Mixer Bus，并通过 `IAudioEffectChainSink` 做 revision-safe 发布。

## 相机与程序化房屋资产

`CameraDocumentTarget` 把 CameraController 的 Rig 和导演时间线变成稳定资产。Rig Inspector
覆盖 follow/orbit/topdown/firstperson/cinematic、目标与偏移、构图、FOV、平滑和速度；时间线
使用稳定 ID 保存 Cut、Event 和 Float Automation。`CameraPreview` 可确定性 scrub，并输出相机、
观察目标和 frustum 线框；`CameraDocumentRuntime` 先完整构建候选控制器，再切换运行 generation。

`HouseGenDocumentTarget` 同时保存组件 Kit 与生成 Request。Kit 可逐项创建，即使尚缺
foundation/floor/wall/door/roof 也能保存并显示诊断；只有请求实际生成预览时才要求 grammar
完整。`HouseGenPreviewRuntime` 在隔离的组件库和布局中执行确定性生成，失败保留上一版，成功后
可输出组件包围盒和房间区域 overlay。模型字段使用 `model3d` AssetRef，生成尺寸、层数、尝试次数
和预览实例数都有明确预算。

`SpriteStackDocumentTarget` 保存 primitive/model 来源、切片轴、层数、分辨率、padding、shade、
tint 及 2D 叠片表现参数。`SpriteStackBakeRuntime` 在 CPU 临时 generation 中完成所有切片，返回
每层 checksum 和 alpha coverage；模型资源通过窄 resolver 提供。完整成功后才能替换旧切片，
并可继续发布成真实 `SpriteStack2D`。512 MiB 解码预算会拒绝烘焙，超过 128 MiB 会提前警告。

`VirtualGeometryDocumentTarget` 保存 Mesh AssetRef、cluster 顶点/三角形上限、LOD 层数、合并因子、
目标简化比，以及预览 FOV、像素误差和距离范围。`VirtualGeometryBuildRuntime` 通过 mesh resolver
进行候选 CPU 构建，检查索引、有限坐标、cluster range 和 child reference，然后输出稳定 checksum、
几何膨胀统计与对数距离 LOD 成本曲线。GPU cache 格式及上传仍由各图形后端/importer 决定。

`AvatarDocumentTarget` 保存 image/Live2D/VRoid 类型和源资产，并用稳定 ID 编辑图层、动态参数和
表达式 channel。Layer Inspector 覆盖纹理、Z、可见性、偏移、尺寸与颜色；Parameter Inspector
覆盖 default/min/max/current。删除仍被表达式引用的图层或参数会被拒绝。`AvatarDocumentRuntime`
先解析所有纹理并完整创建候选 `AvatarInstance`，任一 backend load 或 channel 应用失败都会保留旧 generation。

`VoxelPaletteTarget` 编辑 CubeType 的六面图集 ID、方向性、compose group 和连接提示。方向性类型
在运行时占四个连续 ID，普通类型占一个；Editor 会在超过 255 个 variant 前拒绝操作。
`VoxelPaletteRuntime` 候选构建完整 `CubeTypeRegistry`，并返回稳定 editor ID 到 base ID/variant 数量
的映射，供体素笔刷、调色板 UI 和关卡文档保存引用。

`BiomeDocumentTarget` 把 Procgen 的 BiomeRules 变成稳定资产：Layer Inspector 编辑空间域引用、优先级
和密度，子项 Inspector 编辑加权 prefab/model、缩放范围和随机朝向，exclusion 也使用空间 AssetRef。
结构编辑、属性编辑和快照均可撤销且原子校验；`BiomeDocumentRuntime` 先解析所有空间资源并构建完整
候选规则，成功后才发布。预览按文档 revision 拒绝过期请求，并以固定 seed 确定性生成 PointSet。

`TextureRecipeTarget` 直接读取 Procgen `RecipeDescriptor`，把参数类型、范围、枚举、分类和默认值映射为
通用 Inspector，因此新增 recipe 无需再编写专用面板。参数编辑和快照可撤销且原子校验；
`TextureRecipePreviewRuntime` 在候选图像完整生成后才发布，并返回尺寸、source revision 和稳定像素 checksum。

Network 调试页使用 `Network::telemetrySnapshot()` 获取线程安全复制快照，不持有 socket、channel 或 worker
内部对象。快照包含真实 transport 收发字节、completion/error/connection 累计值、TCP/UDP watch 数、
channel 数和 TCP 待发送字节。`NetworkTelemetryModel` 保存有界历史，计算收发速率和近期错误率，正确处理
计数器 reset，并对高 backpressure、异常错误率及 socket 数量给出结构化诊断。

`SceneImportTarget` 保存 SceneLoader 源 AssetRef 和完整 `LoadOptions`。选择 quality、balanced、mobile 或
raw preset 时会展开为显式选项；随后修改任一选项自动进入 custom，避免 UI 标签与实际导入参数漂移。
`SceneImportPreflightRuntime` 使用 CPU-only `SceneLoader::inspect` 解码，不挂载 ECS 也不上传 GPU，返回
节点/mesh 数、对当前 mounted scene 的 add/remove/modify/move dry-run diff、warning、SOCKET_ 和碰撞节点清单。

`InputMapTarget` 在原始 Keyboard/Mouse/Touch/Joystick API 之上提供设备无关 Action/Binding 资产。
Binding Inspector 支持设备、control、scale、invert 和 deadzone；多个 binding 可合成为一个 1D axis，删除仍被
引用的 action 会被拒绝，重复 control 会产生冲突诊断。`InputMapEvaluator` 可用复制的原始样本确定性预览
action 值；`InputBindingCapture` 过滤设备与微小轴噪声，只捕获首个超过阈值的有意输入。

Profiler 调试页通过 `EditorProfilerCollector` 复制运行时最后一个已完成帧，不保留 Profiler 内部借用引用。
`EditorProfilerModel` 保存有界 CPU/GPU 帧历史，并为最新帧提供按模块、线程和文本筛选的热点表、按
self/total/count/name 排序、模块汇总以及 CPU/GPU/单区段预算诊断。预算与历史容量修改会先完整校验；
过期序列或非法计时不会部分写入。当前运行时聚合行没有稳定父节点身份，因此该模型不构造推测性的调用树。

`Hd2dDocumentTarget` 同时描述 Sprite3D 精灵表和 TileMap3D extrusion preset。Sprite Inspector
覆盖纹理、帧网格、当前帧、动画区间、FPS、翻转、尺寸与 tint；Tile Inspector 覆盖 side depth、
height scale 和 wall UV。`Hd2dFramePreviewService` 可按时间确定性计算帧与 UV，runtime bridge
在纹理解析成功后才创建候选 Sprite3D，或发布配置完整的 TileMap3D builder。

## 对象关系

| 类型 | 职责 |
|------|------|
| `TransformGizmo` | 单对象 TRS/包围盒手柄、射线交互、绘制部件 |
| `GizmoManager` | 多 mode 开关 + attach，转发 pick/drag（Babylon 风格） |
| `TileBuffer` | 与 `map` 解耦的 GID 网格 |
| `Brush` | 笔刷工具；产出 preview / change 列表 |
| `EditorHistory` | 撤销栈；瓦片分组可 `applyLastToBuffer` |
| Toolbar / Inspector / Dock | 仅状态与矩形，由 `ui` 渲染 |
| `ActionTimelineEditor` | 版本化动作时间轴、事务、撤销/重做和确定性预览 |

## 目标导向指南

### 给场景节点加平移旋转缩放

创建 `GizmoManager`，`attach()` 后按需 `setPositionEnabled` / `setRotationEnabled` / `setScaleEnabled`；把相机 `screenToRay` 交给 `pick` / `updateDrag`，再写回 `scene` 节点 local TRS。

### 做简易 tile 地图编辑器

`TileBuffer` + `Brush` + `EditorHistory`：每次 `paintAt` 后把 `getChange*` 记入 `beginGroup`/`recordTile`/`endGroup`；Undo 时 `applyLastToBuffer`。用 `EditorDock` 划分调色板与视口，用 `EditorToolbar` 切换 paint/erase/fill。

## 常见问题

- 期望引擎弹出完整编辑器窗口——本模块只提供构件，需自行用 `ui` / `graphics` 组装。
- 笔刷直接改 `TileLayer`——请先画在 `TileBuffer`，再同步，避免模块硬耦合。
- `getPart*` 在 mode/TRS 变化后过期——调用 `rebuildParts()` 或任意 set*（多数 set 已自动重建）。

## API 快查

- 模块：`newWorkspace` / `newActionTimelineEditor` / `newSession` / `newScriptTool` / `newFieldBrushTool` / `newVolumeBrushTool` / `newConstantBrushFalloff` / `newLinearBrushFalloff` / `newSmoothBrushFalloff` / `newCircleBrushKernel` / `newBoxBrushKernel` / `newSphereVolumeBrushKernel` / `newBoxVolumeBrushKernel` / `newPaintIntFieldOperation` / `newAddScalarFieldOperation` / `newPaintIntVolumeOperation` / `newTileBufferTarget` / `newTileLayerTarget` / `newHeightmapTarget` / `newVoxelWorldTarget` / `registerScriptCommand` / `unregisterScriptCommand` / `newGizmo` / `newGizmoManager` / `newTileBuffer` / `newBrush` / `newToolbar` / `newInspector` / `newDock` / `newHistory` / `applyHeightmapBrush` / `newHeightmapMesh` / `updateHeightmapMesh` / `newHeightmapMeshSmooth` / `updateHeightmapMeshSmooth`
- Workspace：`getId` / `getTitle` / `setTitle` / `registerPanel` / `removePanel` / `clearPanels` / `movePanel` / `setPanelCapability` / `setPanelContext` / `setPanelVisible` / `setPanelSingleton` / `activatePanel` / `getActivePanel` / `getPanelCount` / `getPanelId` / `getPanelTitle` / `getPanelRegion` / `getPanelCapability` / `getPanelContext` / `getPanelOrder` / `getPanelVisible` / `getPanelSingleton` / `setRegionSize` / `layout` / `getRegionX` / `getRegionY` / `getRegionW` / `getRegionH` / `setMode` / `getMode` / `select` / `clearSelection` / `getSelectionCount` / `getSelectionItem` / `getSelectionType` / `getPrimarySelection` / `getSelectionSequence` / `focus` / `getFocusedSurface` / `getRevision`
- 动作时间轴：`configureWorkspace` / `setViewport` / `pointerDown` / `pointerMove` / `pointerUp` / `seekX` / `seekSeconds` / `resizeState` / `undo` / `redo` / `update` / `snapshot` / `play` / `pause` / `isPlaying` / `canUndo` / `canRedo` / `isDragging` / `getDuration` / `getPreviewTime` / `getRevision` / `getAnimationUri` / `getTrackCount` / `getTrackId` / `getTrackLabel` / `getTrackKind` / `getTrackMuted` / `getLayoutWidth` / `getLayoutHeight` / `getPlayheadX` / `getItemCount` / `getItemId` / `getItemType` / `getItemState` / `getItemSelected` / `getItemMinX` / `getItemMaxX` / `getItemMinY` / `getItemMaxY` / `getStateStart` / `getStateEnd` / `getEventCount` / `getEventItemId` / `getEventType` / `getEventTime` / `getEventKind`
- 会话：`addTool` / `addFieldTool` / `addVolumeTool` / `removeTool` / `clearTools` / `bindTileBufferTarget` / `bindTileLayerTarget` / `bindHeightmapTarget` / `bindVoxelWorldTarget` / `clearTarget` / `activateTool` / `getActiveToolId` / `dispatchPointer` / `dispatchPointer3D` / `hasPointerCapture` / `update` / `cancelActiveTool` / `undo` / `redo` / `canUndo` / `canRedo` / `clearHistory` / `getCommandCount` / `getCommandId` / `getCommandName` / `getCommandCategory` / `planCommand` / `executePlan` / `executeCommand`
- 脚本工具：`setShortcut` / `setActivateCallback` / `setDeactivateCallback` / `setPointerCallback` / `setKeyCallback` / `setUpdateCallback` / `setCancelCallback`
- 字段工具：`setRadius` / `setStrength` / `getRadius` / `getStrength` / `setCircleKernel` / `setBoxKernel` / `setPaintIntOperation` / `setAddScalarOperation`
- Kernel：`setConstantFalloff` / `setLinearFalloff` / `setSmoothFalloff`
- 整数字段操作：`setValue` / `getValue`
- 字段 Target：`getTargetId` / `getRevision` / `getWidth` / `getHeight` / `readInt` / `writeInt` / `readScalar` / `writeScalar` / `sampleScalar` / `clearDirtyRegion`
- 体积工具/Target：`setSphereKernel` / `setBoxKernel` / `setPaintIntOperation` / `readInt3` / `writeInt3` / `getDirtyMinX` / `getDirtyMinY` / `getDirtyMinZ` / `getDirtyMaxX` / `getDirtyMaxY` / `getDirtyMaxZ` / `clearDirtyVolume`
- Gizmo：`setMode` / `setSpace` / `setPosition` / `setRotationEuler` / `setScale` / `setBounds` / `setSnap*` / `pick` / `beginDrag` / `updateDrag` / `endDrag` / `getPart*`
- Manager：`set*Enabled` / `attach` / `detach` / `getGizmo` / `pick` / `beginDrag` / `updateDrag`
- Brush：`setTool` / `setSize` / `setShape` / `setTile` / `paintAt` / `eraseAt` / `floodFill` / `paintLine` / `paintRect` / `preview*` / `getChange*`
- History：`push` / `beginGroup` / `recordTile` / `endGroup` / `undo` / `redo` / `applyLastToBuffer`

## 使用要点

- 模块与辅助对象应长期持有；不要每帧 `newGizmo()`。
- 枚举一律 string，非法值抛异常。
- 绘制与输入由宿主完成；本模块不做 GPU 提交。

**源码：** [`src/modules/editor/`](../../../src/modules/editor/)  
**设计文档：** [`docs/dev/编辑器模块设计.md`](../../dev/编辑器模块设计.md)  
**相关测试：** [`test/editor.cpp`](../../../test/editor.cpp)

### PR #287 新增绑定

- 反射链、HDR 与探针相关 API：`getCenterX` `getCenterY`/`getCenterZ` `getColorB` `getColorG`/`getColorR` `getLineCount` `getLineEnd`/`getLineStart` `getStatusLabel` `newReflectionProbeVisualizer`/`setExtents`
