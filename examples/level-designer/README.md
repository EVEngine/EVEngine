# 关卡设计工具（Level Designer）

运行：`make run/win32-debug GAME=examples/level-designer`（其他平台替换构建配置）。
需要 `scene`、`scene_editing`、`scene_editor`、`procgen`、`physics`、`animation`、`model3d`、
`camera`、`ui`、`editor` 及其依赖。

这个例子把已有的可组合组件装配成一个 3D 关卡设计工具：引用地形数据 → 放置白盒快速模型 →
调整模型 → 放置玩家出生点 → 用 Motion Matching 控制器在关卡里自由跑动。

## 编辑器布局

界面按常见关卡 / 场景编辑器壳（Unity / Godot / Unreal 一类）拆成五个区域，由
`editor.newWorkspace` 排布、每个区域一个 UI host（与 `examples/composable-editor` 同壳）：

| 区域 | 面板 | 内容 |
| --- | --- | --- |
| 顶栏 | Toolbar | New / Save / Load · Select / Whitebox / Spawn · Move / Rotate / Scale · Undo / Redo · Play · Frame / Focus |
| 左侧 | Outliner | 地形引用与 Rebuild · 场景层级 |
| 中央 | Viewport | 嵌入式 3D 视口（`ui.viewport` + `renderScene3DToCanvas`）· 状态行 |
| 右侧 | Inspector | 选中物体的名字 / TRS · 白盒 recipe 参数或出生点朝向/人物 |
| 底栏 | Assets | 白盒调色板（Whitebox 工具）或当前工具说明 |

选中物体时会画出可见的 Transform Gizmo（旧版只有拾取、没有绘制）。

## 操作

| 输入 | 作用 |
| --- | --- |
| `1` / `2` / `3` | 切换 选择 / 白盒 / 出生点 工具 |
| `W` / `R` / `T` | Gizmo：移动 / 旋转 / 缩放 |
| 左键（视口内） | 选择工具：拾取物体并开始 Gizmo 拖动；白盒与出生点工具：在鼠标射线与地形交点处放置 |
| 右键拖动（视口内） | 环绕相机（绕当前焦点旋转） |
| 中键拖动（视口内） | **平移相机焦点**（在关卡里横向移动视角） |
| 滚轮 / `Q` / `E` | 拉远 / 拉近 |
| `F` | 聚焦当前选中物体；没有选中时框住整块地形 |
| `Delete` | 删除当前选中节点 |
| `F5` | 进入 / 退出 Play |
| Play 中在视口内移动鼠标 | **视角转动**（第三人称跟随相机绕角色旋转） |
| Play 中 `WASD` + `Shift` | 走 / 跑（方向相对当前视角） |
| Play 中 `Q` / `E` | 拉远 / 拉近跟随距离 |
| Play 中 `F1` / `F2` | 切换玩家人物 / motion 集合 |
| Play 中 `Esc` | 退出 Play |

## 视角调整

编辑视角是「绕一个可平移焦点旋转」的轨道相机：`focusX/Y/Z` 是唯一权威，眼位由
`focus + distance * (cosPitch*sinYaw, sinPitch, cosPitch*cosYaw)` 推出。右键绕焦点转，
中键沿视线的地面基（`forward = (-sinYaw, -cosYaw)`、`right = (cosYaw, -sinYaw)`）平移焦点，
滚轮与 `Q/E` 改距离；平移步长按当前距离缩放，因此在任意缩放下手感一致。`F` 把焦点与距离框到选中物体
（按缩放估算距离），未选中时退回框住整块地形。射线与相机交互都相对**中央视口**的本地坐标
（`ui.viewportMouse*` + `canvas` 尺寸），侧栏不会挡住拾取。

Play 视角是第三人称跟随：`CameraController` 的 `follow` 模式求值 `eye = target + offset`，
所以视角完全用一个 offset 表达：
`offset = (-sinYaw * cosPitch, sinPitch, -cosYaw * cosPitch) * distance`。
`cameraYaw` 同时是**移动方向的基准**（`levelPlayInputYaw`），因此转视角就是转 WASD 的前方；
角色自身仍朝向实际移动方向。鼠标用视口内绝对光标差分（引擎没有提供 delta API），
超过 200px 的跳变会被忽略；指针不在视口上时不转视角。


## 地形数据引用

地形以“引用”的形式存在关卡里，而不是把高度场写进文件：

- `Generated`：由 `seed` + 采样器参数（`frequency`、`octaves`、`lacunarity`、`gain`、
  `ridge`、`warp`、`exponent`、`continent`、`island`、`coast`、`baseLevel`、`amplitude`）
  确定性地重建，关卡文件因此很小且可 diff。
- `Asset file`：引用磁盘上已有的地形资产。`procgen.loadTerrainFile(path, format)` 直接解码
  两种持久化编码——`EVTR` 分块归档（`TerrainAsset::bake` 的产物，UNORM16 量化 + 水文/气候层）
  与 `EVTRN` 原始 float32 高度场（`eve.terrain/1` 资产旁边的 `heightfield.bin`）。`format` 默认
  `auto`，按文件 magic 判定。

世界映射对渲染网格与碰撞体完全一致：

```
worldX = originX + cellX * spacing
worldY = sample * heightScale
worldZ = originZ + cellY * spacing
```

地形网格由 `HeightmapTargetModule.newSmoothMesh` 生成，Play 时的碰撞体是同一个采样面
（`Body3D.newHeightFieldShape`，Box3D 高度场在 XZ 平面上、高度沿 Y），所以“看到的地面”与
“踩到的地面”不会分叉。`spacing` 只有 `EVTRN` 才自带；`EVTR` 不存米/格，此时由关卡自己的
`spacing` 决定（返回值里的 `hasSpacing` 区分这两种情况）。

`assets/terrain/ridge.evtrn` 是一个 65×65、3m 间距的示例地形资产，格式说明见
`assets/terrain/README.md`。**重建失败是原子的**：路径不存在或编码不支持时保留原地形并报出
诊断，不会把关卡拆成半成品。

## 白盒快速模型

调色板来自 ProcgenKit 的 recipe 注册表（`prototype.*`，共 75 件：cube/ramp/stairs/wall/
pillar/fence/…）。关卡里只记录 `{recipe, values}`，网格、渲染实体和 Play 碰撞体都是派生的，
所以关卡文件不内嵌几何。

每件的参数面板由 recipe 自带的 schema 驱动（`getMeshRecipeSchema` 给出 key、类型、
min/max、choice），浮点/整数用 slider、布尔用 checkbox、有限选项用 combo。改参数立即重新生成
网格。

**已知近似**：Play 碰撞体按 width/height/depth 生成盒体，对阶梯、斜面等异形件是包围盒近似，
这是白盒阶段的取舍。

## 调整模型

选中后既可拖 Gizmo（预览 → 释放时提交一次事务），也可在检查器里直接改名字/位置/朝向/缩放。
`Drop to terrain` 把选区贴回地形表面。所有修改都走 `SceneEditorSession` 的命令注册表，
因此撤销重做与冲突检测和 `examples/scene-editor` 完全一致。

## 玩家出生点

出生点是普通场景节点（因此共用同一个 Gizmo、层级与撤销），额外携带设计者元数据：朝向 `yaw`、
以及从这个点开始游戏时应使用的人物与 motion 集合。标记由一个柱体和一个朝向小球表示。

## Play：Motion Matching 控制器

Play 会用**同一份已授权数据**搭出运行时：静态地形高度场 + 每个白盒件一个静态盒体，在选中的
出生点生成玩家。移动由 **Motion Matching 控制器**驱动：

```squirrel
motionDb = animation.newMotionDatabase(skeleton);
motionDb.setRootBoneByName("root");
motionDb.addFeatureBoneByName("foot.l"); motionDb.addFeatureBoneByName("foot.r");
foreach (clipName in levelMotionSetClips(motionSetName)) motionDb.addClip(clip);
motionDb.bake();
matcher = animation.newMotionMatcher(skeleton, motionDb);
matcher.setDesiredVelocity(vx, vz);
matcher.setDesiredYaw(facing);
matcher.update(dt);
```

位移本身交给物理胶囊体（`World3D.moveCapsule` + `isMoverGrounded`），因此角色会被地形起伏和
白盒盒体挡住；姿势来自 `matcher.getPose()`，经 `AnimSkin.applyToMesh` 上传，第三人称跟随相机
用 `CameraController` 的 `follow` 模式。Play 期间不会写入任何已授权的数据，退出即还原编辑器。

`level.play.inputDriven` / `inputForward` / `inputStrafe` / `inputRunning` 让脚本（或 AI、
或冒烟测试）驱动同一条运动路径，而不必模拟按键。

## 人物与 motion 集合

- 人物：`Mannequin`（`Rig_Medium_MovementBasic.glb`）与 `Scout`（`Rig_Medium_MovementAdvanced.glb`），
  都是 KayKit CC0 人形骨架，因此同一套 clip 可以直接驱动两者，不需要重定向配置。
- motion 集合：`Walk`（2 clip）、`Run`（2）、`Locomotion`（4，默认）、`Full`（6，含横移）。
  每个集合各自 bake 一份 `MotionDatabase`——Motion Matching 的质量直接取决于库里有那些 clip。
- 按 (人物, motion 集合) 缓存，切回来是即时的。切到库里没有的 clip 会在启动日志里提示缺了哪几个。

## 关卡文档

存档格式 `eve.level3d` version 1（`level_document.nut`）：

```json
{"schema":"eve.level3d","schemaVersion":1,
 "terrain":{...地形引用...},
 "scene":{...会话 saveJson() 的 eve.scene.hierarchy v1 载荷，原样内嵌...},
 "pieces":[{"object":"..","recipe":"prototype.ramp","values":{...}}],
 "spawns":[{"object":"..","yaw":0.0,"character":"Mannequin","motionSet":"Full"}],
 "player":{"character":"..","motionSet":".."}}
```

**权威划分**：内嵌的 `scene` 载荷仍然是节点身份、父子与 TRS 的唯一权威（它经
`SceneEditorSession` 往返），本文档只负责场景格式刻意排除的语义层——哪个节点是哪件白盒 recipe、
哪个节点是出生点、地形怎么来。未知字段策略：`scene` 载荷内的未知字段由 scene 模块负责保留；
其余位置出现未知字段会以诊断拒绝，而不是静默丢弃。版本不支持时拒绝载入，且**拒绝不会留下
半修改状态**（先校验、后改动）。

存档写到引擎的用户存档目录（与 `examples/scene-editor` 一致，不覆盖项目源文件）：
`eve.Filesystem().setIdentity("level-designer")` → `setupWriteDirectory()` →
`writeTextAtomic("level-designer.json")`。

## 验证

```
make run/win32-debug GAME=examples/level-designer
```
然后在引擎 MCP 里执行 `dofile("smoke.nut");`，或直接把 `smoke.nut` 的内容作为
`eve_run_script` 的 `source` 传入。它检查地形采样与吸附自洽、白盒放置与参数重建、出生点
解析、Play 装配（碰撞体数量、落地、Motion Matching 出姿势）、人物/motion 切换、以及关卡
文档 encode→decode→apply 往返与坏版本拒绝。

C++ 侧（地形文件解码）有独立单元测试：

```
build/win32-debug/test/unit_test.exe --testcase=procgen.terrain.file.decodeEvtrAndEvtrn
```

素材来源与许可见 `assets/kaykit/README.md` 与 `assets/kaykit/LICENSE-KAYKIT-ANIMATIONS.txt`
（CC0 1.0）。

## 快速白盒搭建

启动：`make run/win32-debug GAME=examples/level-designer`。

- 默认进入 Whitebox；资源来自已有 ProcgenKit `prototype.*` 模型库（当前运行时 75 项）。
- 将 Assets 中的资源按钮拖到视口，松手生成模型并切换到 Select。
- Whitebox 模式下，在地面按住左键拖动，实时预览 X/Z 尺寸；松手生成。单击使用默认尺寸。
- Esc 或在视口外松手取消；预览不会修改场景。拖出的高度保持默认，可用 Scale 或模型参数调整。
- Snap 按钮切换 0.5m 网格吸附；Flat work surface 将地形替换为平面，保留现有模型。
- Select 后使用 W/R/T 切换移动、旋转、缩放。Save/Load 沿用现有版本化关卡格式。
- 每次生成是一次场景命令，Undo/Redo 保留白盒 recipe 和模型；历史元数据仅在当前会话存在。

当前吸附目标是地形/平面，不包含模型表面堆叠；拖动按 recipe 默认包围尺寸缩放，不改写其参数。

验证：`build_smoke.nut` 覆盖预览、取消、尺寸、单步撤销/重做、recipe 文档编码及正负网格吸附。
通过 MCP `eve_run_script` 执行 `dofile("build_smoke.nut");`（在空场景运行，会留下一个测试白盒）。
该测试调用与指针共用的手势函数，不代替原生鼠标拖放端到端验证。

### 模型全库验证

在空编辑器中执行 `dofile("model_gallery_check.nut");`，通过真实生成/放置路径建立
75 个模型的检视阵列。成功打印 `MODEL_GALLERY_PASS 75 recipes`；任何失败的 recipe 会单独列出。
2026-10-22 本机实际运行通过：75/75 生成，75 个模型文档 encode/decode/apply 往返恢复，
以及 `build_smoke.nut` 的取消、尺寸、撤销/重做和移动预览回归检查。
修正了移动预览读取已提交坐标、被帧末同步覆盖的问题。全库截图由引擎 `eve_screenshot` 取得。
原生鼠标跨面板拖放仍需独立端到端验证，不能以这些脚本检查代替。

## Agent / MCP 搭建接口

启动编辑器后，agent 可以通过标准 MCP `tools/list` 发现 8 个项目工具；创建后的模型实时出现在
同一个 3D 视口中，可以继续鼠标编辑。此路径不需要 `eve_eval` 或 `eve_run_script`。

```powershell
# 使用 SDK 时先将 bin 加入 PATH；本仓库 Debug DLL 在 build/win32-debug。
$env:PATH = "$PWD/build/win32-debug;$env:PATH"
& build/win32-debug/src/engine/eve.exe run --debug --mcp-port=7529 examples/level-designer
```

将 `mcp.example.json` 中桥接脚本的路径替换为实际仓库路径，加入调用方 agent 的 MCP 配置。
桥接器是仓库已有的 `tools/eve-mcp/server.js`，连接正在运行的编辑器；端口必须一致。
不要让多个引擎共享同一端口。工具注册失败会使启动失败，不会静默降级。

- `whitebox_catalog_v1`：不带参数列出 recipe；传 `recipe` 返回原模型库的参数类型、默认值、边界和选项。
- `whitebox_scene_v1`：返回模型 ID、recipe、参数、TRS 和场景 revision；可按 `object` 查询。
- `whitebox_create_v1`：提交一个完整模型规格，一次生成、一条可撤销场景命令。agent 自定唯一 `id`。
- `whitebox_transform_v1`：按 `object` 修改位置、旋转或缩放，未传字段保留原值。
- `whitebox_delete_v1`：删除指定白盒，UI Undo 可恢复模型及 recipe。
- `whitebox_save_v1`：保存到引擎用户目录，默认 `agent-whitebox.json`；同名文件原子覆盖。
- `whitebox_view_v1`：调整检视相机；之后调用 `eve_screenshot` 获取真实引擎帧。
- `whitebox_flat_surface_v1`：明确请求将地形换为 Y=0 平面；保留已放置模型，不支持撤销地形变更。

例如按顺序调用工具（以下是 MCP `tools/call` 的 params）：

```json
{"name":"whitebox_catalog_v1","arguments":{"recipe":"prototype.stairs"}}
{"name":"whitebox_flat_surface_v1","arguments":{}}
{"name":"whitebox_create_v1","arguments":{"id":"room-floor","recipe":"prototype.cube","position":[0,0.05,0],"scale":[18,0.4,14]}}
{"name":"whitebox_create_v1","arguments":{"id":"entry-stairs","recipe":"prototype.stairs","position":[-5,0.45,-1],"scale":[2,1,2]}}
{"name":"whitebox_view_v1","arguments":{"focus":[0,1,0],"distance":30,"yaw":0.65,"pitch":0.8}}
{"name":"whitebox_save_v1","arguments":{"filename":"my-whitebox.json"}}
```

坐标为米，Y 向上；旋转为 `[pitch,yaw,roll]` 弧度；scale 必须为正。位置为精确世界坐标，
不会暗中吸附地形。`params` 使用 catalog 中的类型（数字、布尔值、字符串）；未传参数使用库默认值。

所有项目工具返回 `{schema:"eve.whitebox.result",schemaVersion:1,ok,revision,...}`。
成功结果在 `value`；失败返回稳定 `code` 和 `message`。客户端必须检查 **`ok`**，不能只检查 MCP
传输是否成功。拒绝未知字段；协议变更使用新工具版本，不静默解释未知字段。
创建/变换/删除可传 `expectedRevision`，不匹配返回 `stale_revision`；revision 指原生场景文档，
不涵盖相机、地形或参数滑块。重复 ID 返回 `already_exists`，不会重复生成；删除后的 ID 在 undo
历史内仍保留。运行中鼠标手势和 Play 模式拒绝 agent 写入（`busy`）。

每次 create 是独立事务；多件场景由 agent 循环调用并逐项检查结果，不声称跨调用原子提交。
输入先验证，网格先生成，然后通过 UI 共用的 `whiteboxPublishPrepared` 发布到
`SceneEditorSession`，由原生场景命令持有 TRS/身份/撤销；没有第二份 agent 场景树。
模型参数继续由原白盒资源库验证/生成，模型 metadata 仍由 `level.pieces` 唯一持有。
这些工具只在主线程同步执行，返回 owning JSON 值，不向调用方暴露 GPU/VM 句柄；热重载重新注册。
存档继续使用 `eve.level3d` v1；未改变引擎 API、模块边界或 ECS 模型，无架构规则例外。

可复现的 agent 示例（要求空编辑器，保留生成结果供检查）：

```powershell
python3 -X utf8 examples/level-designer/agent_demo.py --port 7529 --screenshot build/whitebox-agent-demo.png
```

它从 tools/list 发现接口，通过真实 MCP 创建 10 件房间白盒，执行变换、删除、保存和截图，
并检查 6 种拒绝请求不会改变场景。只依赖 Python 标准库。保存需要标准用户存档目录的写权限；
权限不足会返回 `save_failed`，不会谎报保存成功。


### 多 Agent 原子批量写入

使用 `whitebox_transaction_v1`，不要把多次单对象调用当成一个事务：

```json
{"transactionId":"agent-a-room-001","expectedRevision":12,"operations":[
  {"type":"create","id":"room-floor","recipe":"prototype.cube","position":[0,0,0],"scale":[10,0.2,10]},
  {"type":"transform","id":"existing-wall","position":[0,0,5]},
  {"type":"delete","id":"temporary-block"}
]}
```

先读取 `whitebox_scene_v1` 的 revision。每批 1–128 项，每个对象最多出现一次。
所有参数先校验，生成资源暂时隐藏，整批节点通过原生 `scene.snapshot.restore.v1`
提交一次，成功后发布准备好的 metadata；撤销/重做覆盖整批。创建可携带 recipe 参数，
本接口不修改已有模型的 recipe 参数、不混入地形或文件写入。未知字段和操作拒绝。
生成中途或原生命令失败时不发布节点/metadata；已准备的隐藏 renderer 留在可复用池。

引擎主线程串行处理多个客户端请求。两批使用同一旧 revision 时只允许一批成功，
另一批返回 `stale_revision`，Agent 必须重新读取并重新规划，不会自动覆盖。
相同 transactionId + 完全相同请求返回原回执；内容不同返回 `transaction_id_conflict`。
回执中的 `committedRevision` 是原提交版本，外层 `revision` 是当前版本。
重试已撤销事务也只返回原回执，不再次执行。回执仅保留在当前编辑器会话，重启/脚本重载不保留；
最多 4096 条，满后显式拒绝新事务，不能把它当成跨重启的持久 exactly-once 协议。

验证：

```powershell
python3 -X utf8 examples/level-designer/transaction_check.py --port 7529
```

该检查使用两个真实 TCP 客户端并发提交，覆盖冲突重试、失败后同 ID 重试、精确请求匹配、混合操作、拒绝时完整场景不变。
`transaction_smoke.nut` 在专用测试会话运行，是诊断故障注入脚本，覆盖第二次网格生成抛异常、
资源准备后原生命令拒绝，以及整批 undo/redo
和模型 metadata 一致性；它不属于 Agent 正常创作路径。
C++ `test/editor_authority_atomic.cpp` 覆盖候选操作异常、发布失败、并发版本冲突、计划校验、
旧目标回滚异常传播。候选发布由现有 `IDomainOperationTargetStaging` 提供，未引入第二套场景 owner。


独立原生契约测试（编译当前源码和 zeroerr，不借用旧引擎 DLL）：

```powershell
cmake/with-msvc.cmd cmake -S test/transaction_contracts -B build/transaction-contracts -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake/with-msvc.cmd cmake --build build/transaction-contracts -j 4
ctest --test-dir build/transaction-contracts --output-on-failure
```

组合验收必须使用重新编译的 `eve`，同时运行 `editor.atomic.*`、`editor.v2.*`、
`scene.transaction.*` 和 transaction 原生测试，再运行上述真实 MCP 并发与故障注入检查。
`model_gallery_check.nut` 在空场景生成全部白盒 recipe；截图必须来自引擎 `eve_screenshot`。

`presentationError` 非空表示事务已提交但界面同步报错，调用方应保留原回执，不得按拒绝重新创建。
