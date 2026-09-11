# 关卡设计工具（Level Designer）

运行：`make run/win32-debug GAME=examples/level-designer`（其他平台替换构建配置）。
需要 `scene`、`scene_editing`、`scene_editor`、`procgen`、`physics`、`animation`、`model3d`、
`camera`、`ui`、`editor` 及其依赖。

这个例子把已有的可组合组件装配成一个 3D 关卡设计工具：引用地形数据 → 放置白盒快速模型 →
调整模型 → 放置玩家出生点 → 用 Motion Matching 控制器在关卡里自由跑动。

## 操作

| 输入 | 作用 |
| --- | --- |
| `1` / `2` / `3` | 切换 选择 / 白盒 / 出生点 工具 |
| 左键 | 选择工具：拾取物体并开始 Gizmo 拖动；白盒与出生点工具：在鼠标射线与地形交点处放置 |
| 右键拖动 | 环绕相机（绕当前焦点旋转） |
| 中键拖动 | **平移相机焦点**（在关卡里横向移动视角） |
| `Q` / `E` | 拉远 / 拉近 |
| `F` | 聚焦当前选中物体；没有选中时框住整块地形 |
| `Delete` | 删除当前选中节点 |
| `F5` | 进入 / 退出 Play |
| Play 中移动鼠标 | **视角转动**（第三人称跟随相机绕角色旋转） |
| Play 中 `WASD` + `Shift` | 走 / 跑（方向相对当前视角） |
| Play 中 `Q` / `E` | 拉远 / 拉近跟随距离 |
| Play 中 `F1` / `F2` | 切换玩家人物 / motion 集合 |
| Play 中 `Esc` | 退出 Play |

## 视角调整

编辑视角是「绕一个可平移焦点旋转」的轨道相机：`focusX/Y/Z` 是唯一权威，眼位由
`focus + distance * (cosPitch*sinYaw, sinPitch, cosPitch*cosYaw)` 推出。右键绕焦点转，
中键沿视线的地面基（`forward = (-sinYaw, -cosYaw)`、`right = (cosYaw, -sinYaw)`）平移焦点，
`Q/E` 改距离；平移步长按当前距离缩放，因此在任意缩放下手感一致。`F` 把焦点与距离框到选中物体
（按缩放估算距离），未选中时退回框住整块地形。

Play 视角是第三人称跟随：`CameraController` 的 `follow` 模式求值 `eye = target + offset`，
所以视角完全用一个 offset 表达：
`offset = (-sinYaw * cosPitch, sinPitch, -cosYaw * cosPitch) * distance`。
`cameraYaw` 同时是**移动方向的基准**（`levelPlayInputYaw`），因此转视角就是转 WASD 的前方；
角色自身仍朝向实际移动方向。鼠标用绝对光标位置做差分（引擎没有提供 delta API），
超过 200px 的跳变（进出窗口、切换程序）会被忽略以免视角被甩飞；指针悬停在编辑面板上时不转视角。


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
