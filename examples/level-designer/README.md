# Terrain & Scene Studio：原生 3D 地形、分层建造和物理编辑器

基于 EVEngine 的原生编辑器，运行于引擎进程内。入口仍为 `examples/level-designer`：

```bash
make run/linux-debug GAME=examples/level-designer
# Windows: make run/win32-debug GAME=examples/level-designer
# macOS:   make run/macosx-debug GAME=examples/level-designer
```

启动后显示可编辑的庭院示范场景：基础平台、墙体、嵌套亭子组合、可行走斜坡、动态箱体和人物出生点。
`New` 创建空场景，`Demo` 重新载入示范。示范也通过普通编辑命令创建，所有对象都可继续编辑。

## 五个工作区

顶部为文件、工具、变换和试玩操作；左侧为地形来源、当前编辑层级和对象树；中央为嵌入式 3D 视口；
右侧为对象名称、变换、物理类型和程序化参数；底部为当前工具的建造素材、笔刷或建造块库。
方向光和深度绘制用于辨认空间关系；动态物体为暖橙色，静态零件为蓝灰色。
嵌入视口采用前向预览，不启用运行时主画面的阴影 / G-buffer / AO；Gizmo 和笔刷圈以独立辅助图元叠加。

| 工具 / 输入 | 操作 |
|---|---|
| `1` Select | 选择；拖动可见 Gizmo；`W/R/T` 移动 / 旋转 / 缩放 |
| `2` Whitebox | 在 75 个程序化 recipe 中选择零件，点击地形放置，检查器调参数 |
| `3` Character | 点击地形放置人物出生点；选择人物、动作集和出生朝向 |
| `4` Terrain | 按住左键抬升、下陷、平滑或整平；圈线表示世界单位笔刷半径 |
| `5` Blocks | 选择已捕获的建造块，点击地形放置独立实例 |
| 右键拖动 / 中键拖动 / 滚轮 | 环绕 / 平移 / 缩放视角 |
| `F` | 聚焦选择；没有选择时框住地形 |
| Ctrl + 点击 | 多选同层对象（视口或层级树） |
| `Ctrl+G` / Group | 把选中的同层零件或组合抽象为更高一级建造块 |
| `Enter` / Enter 按钮 | 进入选中建造块，编辑内部下一层对象 |
| `Backspace` / Up | 回到上一层 |
| `Unpack` | 拆开组合，保留子对象世界姿态 |
| `Capture` | 将当前零件或整个嵌套子树捕获到可复用建造块库 |
| `Ctrl+D` / Duplicate | 独立复制整个选中子树，平移 3m |
| Delete | 删除选中对象及其子树；可撤销 |
| `Ctrl+Z/Y` / Undo / Redo | 普通工具操作场景历史；Terrain 工具操作地形笔画历史 |
| `Ctrl+S` / Save / Load | 保存 / 恢复包含地形样本和建造块库的关卡文档 |
| `F5` / Play | 进入 / 退出真实物理试玩 |

工具栏、层级树和素材库使用引擎内置的 Font Awesome 语义图标。常用文件 / 历史 / 聚焦操作采用紧凑图标按钮，悬停显示操作与快捷键；编辑工具保留文字，选中状态通过高亮显示。人物、零件和组合分别使用不同对象图标，图标按钮均保留可访问名称。

## 一层一层抽象场景

例如先将立柱、梁和屋顶组合成“亭子”，进入亭子调整内部零件，再回到世界，把亭子、墙和地面组合成
“庭院”。可继续把庭院放入更高一级场景。组合、拆开和整棵子树删除各为一次 SceneEditorSession 事务，
支持撤销重做、修订检查和稳定 ID。选中块时视口拾取以当前编辑层级为界；层级树可以直接选择任意深度。

`Capture` 保存独立的子树快照及程序化参数、物理类型和人物配置。放置实例产生新的稳定 ID，修改原块
不会影响已放置实例。它不是联动更新的 prefab 实例。建造块库随关卡文件保存。

对象树是父级和 TRS 的唯一权威。渲染和碰撞均使用组合后的世界变换，Gizmo 预览也会传播到子孙。
组合采用统一缩放；不能无损表示为刚体姿态的层级剪切和镜像会明确拒绝，避免画面与碰撞静默分叉。

## 地形编辑

抬升 / 下陷以米每秒工作，半径以米表示。平滑先读取所有邻点再发布，整平高度取自笔画起点。
每次完整笔画可一次撤销，保留最近 32 笔；网格上传合并到最多 30 Hz。笔刷离开视口或释放鼠标时结束笔画。

保存的是原生 `eve.terrain-editor-document/1` 编码的实际高度样本，重新打开不会丢掉雕刻结果。
地形采样、放置和 Play 高度场共用高度图、原点、间距及高度比例。Play 期间禁止编辑；退出后可继续雕刻，
再次 Play 使用更新后的地形。地形来源支持确定性生成参数或 EVTR / EVTRN 文件；不支持非等距 XZ 网格，
重建失败保留原地形。来源设置在点击 Rebuild 前仅为草稿，不改变正在编辑的地形。

## 物理交互

检查器的 Body 可选 `Static` / `Dynamic`。静态零件使用实际程序化网格的三角面碰撞，因此斜坡、楼梯和
门洞可按其可见轮廓交互；动态物体使用最多 64 顶点的凸包，凹形空隙会被填充，当前不执行离线凸分解。
几何只在参数变化时准备，不在每帧重新生成。

Play 使用同一个调用方拥有的 World3D：地形高度场、所有建造块碰撞体和真正的动态人物胶囊在同一世界中
以固定 60 Hz 推进。人物可推动箱体，箱体也会受到重力、接触和摩擦影响；人物外观由 KayKit 骨架和
Motion Matching 动画驱动。多个出生点中优先控制选中者，其次控制第一个；其他放置的人物也会生成独立动态胶囊，可以受到物体和玩家推动。
编辑模式直接预览所选角色模型，每个人物使用独立的模型与动画缓存。

| Play 输入 | 操作 |
|---|---|
| WASD / Shift / Space | 走动 / 跑动 / 跳跃 |
| 视口内移动鼠标 / Q、E | 第三人称视角 / 跟随距离 |
| F1 / F2 | 切换人物 / 动作集 |
| Pause / Resume | 暂停 / 继续物理 |
| Step | 暂停并前进一个 1/60 秒物理步 |
| Esc / F5 / Stop | 销毁试玩世界并恢复编辑姿态 |

试玩不写入场景文档。停止后动态物体回到编辑时的位置；不自动烘焙模拟结果。固定时间步用于稳定交互，
不承诺不同物理后端或硬件之间逐位确定性。当前人物为直接速度控制的动态胶囊，不是攀爬或自动跨台阶控制器。

## 文档、所有权与失败语义

关卡文件为 `eve.level3d` schemaVersion 2，保存到应用数据目录 `level-designer.json`，不覆盖源码。
包含 scene 层级、地形来源及实际 surface、pieces 的 recipe / values / motion、spawns、player、library。
文档 JSON 复用 common 的原生 `eve.json.encode/decode`（统一 Value / Result、有限值与重复键检查），
避免在脚本中逐字拼接大地形字符串；中文名称和 Unicode 转义可以无损往返。
v1 迁移为静态零件和空库，按旧地形引用重建。未知顶层 / 语义字段、未知版本、无效资源、坏层级或无效物理类型
均拒绝。加载先离线检查层级、几何变换和记录，再暂存地形和网格，成功才替换可见文档；失败保留旧文档。
加载 / New 建立新的历史基线。地形笔画与场景事务分别有历史；程序化参数及 Body 设置暂不加入场景撤销历史。

SceneEditorSession 拥有节点和 TRS；地形模块拥有高度图；脚本拥有建造块语义；渲染物体、网格及碰撞是派生投影。
删除后的语义记录保留到 New / Load，以便撤销恢复。Play 拥有且显式销毁运行时世界，停止后丢弃其所有 Body 引用。
所有操作在引擎 owner thread 运行，不增设通用 GameObject 或 provider registry。

## 验证

在真实引擎启动后，通过 MCP `eve_run_script` 执行 `dofile("studio_smoke.nut");`。
它先运行原有 `smoke.nut`，再验证笔画撤销、地形样本存档、嵌套组合与旋转缩放、独立实例、删除撤销、
v1 迁移、坏资源原子拒绝、刚体下落和地面接触、人物推动箱体、暂停单步以及 Stop 恢复。
场景验证脚本会重建当前文档，请在测试实例运行。

```bash
python3 scripts/check_examples.py
MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh level-designer
make check
```

素材来源与许可见 `assets/kaykit/README.md` 与 `assets/kaykit/LICENSE-KAYKIT-ANIMATIONS.txt`
（CC0 1.0）。

## 快速白盒搭建

启动：`make run/win32-debug GAME=examples/level-designer`。

- 切换到 Whitebox；资源来自已有 ProcgenKit `prototype.*` 模型库（当前运行时 75 项）。
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
存档沿用 Studio 的版本化 `eve.level3d` 格式；未改变 ECS 模型，无架构规则例外。

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
C++ `test/integration/transaction-authority/editor_authority_atomic.cpp` 覆盖候选操作异常、发布失败、并发版本冲突、计划校验、
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
`level_blocks.nut` 为组合与库，`level_sculpt.nut` 为笔刷，`level_demo.nut` 为示范装配。
人物与动作素材来源见 `assets/kaykit/README.md`（CC0）。
