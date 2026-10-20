# RPG Atelier — 探测问题清单

在实现 `examples/rpg-atelier`（角色 / 战斗 / 背包 / 对话 / 地图编辑 / 场景编辑）并尝试
在本 Cloud VM 上构建、运行的过程中记录。状态随联调更新。

## P0 — 阻塞构建 / 运行

| ID | 类别 | 问题 | 证据 / 影响 |
| --- | --- | --- | --- |
| B1 | 集成冲突 | 工作区 `external/ECS.hpp` 子模块曾被推进到 `47fe297`（去掉 `ViewIterator::entity()`→`B*`），而 `dev` 钉的是 `9949054`；`RPGActor::liveActors()` 因此 **无法编译** | `/tmp/eve-build.log`：`push_back(it.entity())` 类型错误；需 `git checkout 9949054` + 重新打 `ecs-shared-default-table.patch` |
| B2 | 环境 | 首次 `make debug` 会 clone/编译 third-party，耗时长；Cloud VM 需 `JOBS=4` | 构建日志从 deps 开始数十分钟级 |

## P1 — 架构 / API 面

| ID | 类别 | 问题 | 影响 |
| --- | --- | --- | --- |
| A1 | 架构 | RPG 玩法（`rpg`/`inventory`/`dialogue`）、瓦片地图（`map`）、关卡笔刷（`levelEditor`）、3D 场景导演（`scene_director`）分属不同宿主包；**没有一等「RPG 工坊」组合面** | Agent 必须自己拼 4+ 条协议；本示例是胶水，不是引擎产品 |
| A2 | 架构 | `Brush`/`EditorHistory` 操作的是 **独立 `TileBuffer`**，需脚本手写循环 sync 回 `TileLayer`；文档也承认这是「很小的纯 tile 工具」 | 地图编辑与运行时权威层双写，易漂移；Undo 还要 `applyLastToBuffer` |
| A3 | 架构 | `scene_director` 靠 `compilestring(eve.sceneDirectorScript)` 注入，不是稳定模块槽位 API | 与 `eve.Map()` / `eve.RPG()` 发现路径不一致；失败时只能 log |
| A4 | 功能缺失 | `eve_gameplay` 已覆盖 inventory/dialogue/economy/card，**没有 rpg/battle/quest 域的 publishGameplay** | Agent 不能用同一套 observe/submit 驱动战斗与任务，只能旁路脚本或 MCP 自定义 tool |
| A5 | API 质量 | Binding Contract 里 `dialogue/startChecked` 参数表为空，实际要 `(id, {})`；`takeDamage` 要 `(resource, amount, source)`；`registerSkillDamage` 是字符串公式不是 Squirrel 闭包，但模型常猜成回调 | Agent 易按 JSON 合同或 Unity 习惯误调；需对照金样例 |
| A6 | API 质量 | `levelEditor.newTileBuffer` 在合同里落在 `map/newTileBuffer` 命名空间，槽位名与合同 key 前缀不一致 | 发现成本高 |
| A7 | 功能缺失 | 地图对象编辑（放 NPC / 遭遇 / portal）没有与 Brush 同级的脚本一等 API；本示例只能改 GID | 「地图编辑」半完成：地形可画，事件对象不能可视化摆 |

## P2 — 体验 / 质量 / 性能

| ID | 类别 | 问题 | 影响 |
| --- | --- | --- | --- |
| Q1 | 体验 | 已有 `rpg-classic`（完整产品环）与 `ai-stage`（场景）分离；做「又玩又编」要新建胶水工程 | 本 atelier 即为此缺口 |
| Q2 | 体验 | 模式键 F1–F4、地图编辑鼠标、场景编辑键位互相抢键；战斗与对话都用 `1` | 集成 UI 未统一输入路由 |
| Q3 | 代码质量 | `rpg-classic/main.nut` >2000 行单文件；新工坊若继续堆会难维护 | 应用领域脚本仍缺推荐分包边界 |
| Q4 | 性能 | MapEdit 拖拽时对整层 `setTile` 全量 sync + `pathfinder.syncFromLayer` | 大地图会卡；缺脏矩形路径在脚本示例中的用法 |
| Q5 | Bug 风险 | 无图集纹理时 `map.render` 仍画逻辑格，但视觉只有色块/空白；GID 1/2 无 tileset 时「看不出编辑结果」依赖自定义 marker | 本示例用实体色块补偿，非引擎默认 |
| Q6 | 集成 | `game.agent.json` 观察 `mode` 字符串根路径与 ai-game 的 `gameState` 表路径模型不一致 | Play Host 合同样例不足，Agent 难复用 |
| Q7 | Bug | UI widget id 与 `persist` 同名（`log`）会污染根表：`log.len()` 变成「index len 不存在」 | smoke 首帧崩溃；须避免 widget id 撞脚本状态名 |
| Q8 | API | `dialogue.clear` 不存在，应用 `dialogue.reset`；合同/文档易让人猜错 | `eve_init` 抛错后仍继续进帧 |
| Q9 | API | `publishGameplay` 挂在 `dialogueFlow` 上，不在 `dialogue`；合同 key 却是 `dialogue/publishGameplay` | Agent 按模块名调用会 miss；与 inventory 挂在 `Inventory` 实例的模式也不对称 |
| Q10 | API | 地图 object-contract 必须是 `{schema,version,types:[{type,properties:[{name,kind}]}]}`；猜成 `schemaId` / map-of-types 会被硬拒 | 无契约样例时 Agent 一次写不对；错误只给 `$contract.schema` |
| Q11 | 性能/架构 | 在 MCP `tools/call` 里同步 `ensureScene()`/`scene_director` 安装会卡住 JSON-RPC（主线程 poll 内重活） | 实测 `atelier_set_mode(scene)` 后下一 RPC 超时；需把重初始化推迟到 `eve_update` |

## P3 — 与 Godot MCP / 既有结论相关

| ID | 类别 | 问题 |
| --- | --- | --- |
| G1 | 体验 | Cursor 不刷新 tools/list；动态 `eve.mcp.tool` 注册依赖客户端再 list |
| G2 | 体验 | 复杂 RPG 仍缺「编辑器优先 + Undo」统一命令面；atelier 的 MapEdit 是运行时旁路 |

## 实现结果（本分支交付）

- 新增 `examples/rpg-atelier/`：四模式一体示例 + 数据 + README + FINDINGS
- `examples/README.md` 登记行
- 构建：修复本地脏 ECS 子模块（`47fe297`→钉住的 `9949054` + DLL patch）后 `make debug` 产出 `eve`
- 验证：
  - `scripts/smoke_examples.sh rpg-atelier` → **PASS**（≥3s，无 error marker）
  - MCP：`atelier_status` / `atelier_set_mode` 可用；`map_edit`/`scene` 切换成功；`sceneDirector=true`
  - `eve_gameplay op=instances` 可见 `inventory` + `dialogue` 已发布实例（无 rpg/battle 域）
