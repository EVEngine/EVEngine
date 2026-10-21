# Agent 任务剖面（Task Profile）与少 Token 内容面 — 修改方案

日期：2026-10-20  
状态：Slice A 已落地（session `taskKind`/`profile`、filter 叠加、`eve-mcp --profile=`、调用拒绝带 escalate）；B–E 未开始  

来源：会话结论（按任务主动裁切工作面 / recipe+约束 / 少写脚本）+ Godot MCP 实探与 EVEngine atelier/bench 问题汇总  
相关：[`AI知识补偿.md`](AI知识补偿.md)、[`Agent游戏开发范式.md`](Agent游戏开发范式.md)、[`2026-09-05-play-verify-evolve.md`](2026-09-05-play-verify-evolve.md)、[`AI与MCP支持.md`](AI与MCP支持.md)、[`AI场景导演.md`](AI场景导演.md)、[`2026-08-29-agent-native-engine-architecture.md`](2026-08-29-agent-native-engine-architecture.md)

## 1. 一句话目标

> 让 Agent **先选任务剖面、再碰最小工具面**；复杂内容用 recipe/约束展开，脚本与全量 MCP 清单不再进默认上下文。

不是移植 Godot 的 `enable_toolset` 开闸协议，也不是再堆一批平行 MCP 工具。

## 2. 问题映射（汇总 → 本方案）

| 汇总编号 | 痛点 | 本方案应答 |
| --- | --- | --- |
| C1 / GM7 / EM2 | Cursor 不刷新 `tools/list`；动态注册/enable 后 Agent「看不见」 | **连接时剖面** + **调用时强制**；动态增工具用稳定 facade，不依赖客户端再 list |
| GM5–GM8 | Godot 强制 list→enable→call，toolset 多、开闸烧轮次 | **禁止 Godot 式 enable 门控**；剖面在 `session_start` / 环境变量一次选定 |
| C2 / EA1 | 缺「编辑器优先 + 玩法」统一命令面 | 工坊剖面只暴露少量组合 facade（play/edit/recipe），内部转发已有协议 |
| EM1 | MCP 回调里同步重初始化卡死 JSON-RPC | 重活一律推迟到 `eve_update`；工具立即返回 `pending`/`retryable` |
| EM3–EM4 | gameplay 域不全；`game.agent.json` 观察路径样例分裂 | 补 rpg/battle/quest 发布面；统一 observe 根路径合同 |
| EM5–EM12 | Binding Contract 与真实 API 漂移 | **合同真理修复**单独立项，Agent 查合同即真相 |
| EA2–EA7 / EQ* | 地图双写、scene_director 注入、证据弱 | 跟随后续切片；本方案只定「剖面内允许的入口」 |
| C3 | Token 难公平计量 | 本方案不解决计费；用 **工具调用次数 + 剖面宽度 + 产出字节** 作代理指标写进 session |
| GM* / GQ* | Godot 侧问题 | **不做**；仅作反例约束 EV 设计 |

## 3. 设计原则（会话结论固化）

1. **Token 买选择与约束，不买几何与 API 复述。**  
2. **按任务主动裁切工作面**；扩面必须有错误码与 escalate，禁止默认 `all`。  
3. **内容走 recipe / 领域命令；脚本只写不可数据化行为。**  
4. **可证伪**：required criterion 必须来自 `eve_play` / `eve_gameplay` / 引擎截图 / 测试，禁止 `eve_eval` 过关。  
5. **反 Godot 开闸**：不引入「先 enable 29 个 toolset」；不把 schema 藏在 enable 返回体里。

## 4. 核心机制：Task Profile

### 4.1 任务种类（`taskKind`）

`eve_agent_session_start` 增加必填（或强烈推荐）字段：

```json
{
  "objective": "…",
  "taskKind": "content | script | verify | fix | workshop",
  "profile": "content.scene | content.map | script.bind | verify.play | fix.compile | workshop.rpg",
  "criteria": [ … ]
}
```

| `taskKind` | 默认 `profile` | Agent 允许做什么 | 默认禁止 |
| --- | --- | --- | --- |
| `content` | `content.scene` / `content.map` | recipe、`eve_scene_batch`、map/editor 命令、摘要 info | 写长脚本、全量 debug、VLM |
| `script` | `script.bind` | `eve_api_search/get`、编译/诊断、最小文件编辑线索 | 全场景 batch、截图叙事 |
| `verify` | `verify.play` | `eve_play` / `eve_gameplay` / 语义 observe / 可选一张截图 | 搭景、改 Binding |
| `fix` | `fix.compile` | console、error_slice、单条 api_get、软重启 | 扩到 content 除非 escalate |
| `workshop` | `workshop.rpg` | 组合 facade：编辑切片 + 玩法 observe/submit | 裸 `eve_eval`、散落 4+ 协议手拼 |

未指定 `taskKind` 时：兼容旧客户端，默认 `verify`（最窄可玩验证面），**不要**默认 `all`。

### 4.2 剖面与现有 `EVE_MCP_TOOLSET` 的关系

已有环境变量过滤（`EVE_MCP_TOOLSET` / `READONLY` / `EXCLUDE`，见 `McpToolFilter`）保留为 **进程启动默认剖面**。

会话级剖面叠在其上：

```text
allowed(tool) = envFilter(tool) ∧ sessionProfile(tool) ∧ ¬exclude
```

| 来源 | 作用时机 | 是否依赖 Cursor 再 list |
| --- | --- | --- |
| `EVE_MCP_TOOLSET=…` | 进程启动，`tools/list` 已裁切 | 否（连接时即窄） |
| `session.profile` | `session_start` 后 | **调用强制始终生效**；list 变更尽力而为 |
| 脚本 `eve.mcp.tool` | 运行时注册 | 见 §4.4，挂到 facade，不扩大 list |

### 4.3 对抗 C1：双轨，不赌客户端刷新

Cursor 不刷新 `tools/list`（C1）是外部约束。EV 侧必须同时成立：

**轨 A — 连接时窄 list（省 token）**  
推荐 Agent / CI / `tools/eve-mcp` 启动时带：

```bash
EVE_MCP_TOOLSET=agent,core,play   # 例：验证会话
# 或
EVE_MCP_TOOLSET=agent,core,scene  # 例：搭景
```

`eve-mcp` 可增加 `--profile=`，内部设置同名环境或握手参数，避免人类手写 env。

**轨 B — 调用时强制（正确性）**  
即使客户端仍缓存了完整工具表：

- 剖面外 `tools/call` → `{ ok:false, filtered:true, profile, escalate?: { to, reason, errorCode } }`
- **绝不** silent no-op（对照 GM6）
- 允许的 escalate 边写死在剖面图里（例如 `content.scene` → `verify.play` 仅在 layout 已 apply 后）

**轨 C — 可选通知（增强，非依赖）**  
剖面变更时发送 MCP `notifications/tools/list_changed`（若传输层已支持）。  
Cursor 忽略则系统仍靠轨 A/B 正确；**不得**把产品正确性绑在通知上。

### 4.4 禁止「动态工具膨胀 list」

对照 EM2 / atelier 自定义 `atelier_*`：

- 游戏专用工具默认 **不** 直接 `eve.mcp.tool` 进根 list。
- 改为挂到稳定 facade，例如：
  - `eve_workshop` `{ "op": "status|set_mode|…", … }`
  - 或 `eve_play` / `eve_gameplay` 已有 op 扩展
- 若必须注册具名工具：仅在对应 `workshop.*` 剖面下出现在 **下一次** `tools/list`；调用路径仍经 facade 转发，以便旧 Cursor 缓存也能用 facade 名调用。

### 4.5 剖面标签扩展（在现有 tag 上长）

现有 tag：`core,debug,play,editor,host,scene,input,domain,agent`（`AI与MCP支持.md`）。

增量（实现时可映射到 tag 集合，不必新增 29 个 Godot 式 set）：

| profile | 大致 tag 并集 | 额外白名单工具 |
| --- | --- | --- |
| `content.scene` | `agent,core,scene` | `eve_scene_batch`, `eve_procgen_recipes`, recipe apply（新） |
| `content.map` | `agent,core,editor`（地图相关子集） | map/brush 命令 facade |
| `script.bind` | `agent,core,debug`（只读诊断 + api_*） | `eve_api_search`, `eve_api_get`, `eve_console_read` |
| `verify.play` | `agent,core,play,input` | `eve_play`, `eve_gameplay`, 可选 `eve_screenshot` |
| `fix.compile` | `agent,core,debug` | console / error_slice / `eve_restart` |
| `workshop.rpg` | 上述的受控并集 | **仅** `eve_workshop` + play/gameplay + 必要 editor op |

`eve_eval` / `eve_run_script`：所有剖面默认 **拒绝** 作为 required 证据路径；仅 `fix.compile` 在显式 `danger=true` 且非 criterion 证据时可调用（写进 session 警告）。

## 5. 少 Token 内容面（与剖面正交）

剖面解决「看见什么工具」；本节解决「改内容时说什么」。

### 5.1 Recipe 作为对话单词

新增（或收口已有 procgen）合同：

```text
eve_recipe_list
eve_recipe_apply { "id": "ruins.moonlit", "params": { "seed": 7, "trees": 3 }, "expectedRevision"? }
```

- `id` 来自注册表（引擎/示例/项目 `recipes/`），不是模型自由字符串拼几何。
- 一次 apply = 一次领域命令事务；失败不留半局。
- Agent 默认输出：`recipe id + 少量 params + 约束`，不是 prop 列表。

### 5.2 约束验收（verify 剖面）

在 `game.agent.json` / session criteria 中鼓励声明：

- `reachable(target)`、`dialogue.reached(node)`、`inventory.has(item)`、`battle.ended`

由 `eve_play` / `eve_gameplay` observe 判定；布局问题回到 content 剖面用 recipe 参数改 seed，而不是让模型重写坐标。

### 5.3 摘要进上下文，全量进引擎

- Discover：`eve_game_world` + `eve_scene_info` **摘要字段**（count、revision、选中 id）。
- 禁止把完整场景 JSON / 全 Binding Contract 灌进 system prompt。
- 脚本面只 `eve_api_search` 命中条。

## 6. 合同与玩法面修复（与剖面并行的硬依赖）

无下列修复时，剖面只会「正确地走到错误 API」。

### 6.1 Binding Contract 真理（EM5–EM12）P0

| 项 | 动作 |
| --- | --- |
| `dialogue.clear` → `dialogue.reset` | 合同与金样例对齐；错误提示给 did-you-mean |
| `publishGameplay` 挂在 `dialogueFlow` | 合同 key / 槽位名一致；文档与 `eve_api_get` 同真相 |
| `startChecked` 参数表 | 生成器必须导出实际参数，禁止空 properties |
| `takeDamage` / `registerSkillDamage` | 签名与「字符串公式非闭包」写入合同 `description` |
| `levelEditor` / `map` 前缀错位 | 合同命名空间与根槽位一致 |
| object-contract 拒识 | 错误返回期望 schema 摘要，不只 `$contract.schema` |
| UI id 与 `persist` 撞名 | 运行时检测 + 示例规范（禁止 `log` 等根表污染） |

验收：未读过仓库的 Agent 仅靠 `eve_api_search` 写出通过编译的最小对话/伤害调用（对齐 `AI知识补偿.md` 成功标准）。

### 6.2 Gameplay 发布面（EM3）P0

为 rpg / battle / quest（或现有模块的规范名）实现与 inventory/dialogue 相同的 `IGameplayControlProvider`：

- `observe` / `actions` / `submit` / `advance` / `events`
- 实例路由与 revision 账本与现网一致

无 publish 的域不得出现在 `eve_game_world.domains` 里假装可驱动。

### 6.3 Play 观察路径合同（EM4）P1

`game.agent.json` 观察根统一一种：

- 推荐：`paths.gameState` 表路径（与 `examples/ai-game` 对齐）
- 废弃散落的裸 `mode` 字符串根；迁移期双读并在 `eve_play status` 打 `deprecated`

金样例：至少一个 RPG/工坊示例与 ai-game 共用同一观察形状。

### 6.4 主线程重活（EM1）P0

所有 `ensure*` / kit install / 重编译触发：

- MCP 回调只入队；
- 返回 `{ ok:false, retryable:true, reason:"deferred-to-update" }` 或 `{ ok:true, pending:true }`；
- 单测：`tools/call` 后下一 RPC 不得超时。

## 7. 工坊组合面（C2 / EA1）— 有意做窄

**不**新建顶层 `rpg_atelier` 模块包（遵守模块编排：卫星进宿主）。  
新增的是 **MCP/脚本 facade 合同**，背后调用已有 map / dialogue / inventory / rpg / editor 命令。

```text
eve_workshop
  op: status | set_mode | recipe.apply | map.brush | play.observe | play.submit
  profile: workshop.rpg 下可见/可调
```

模式键冲突（EA5）、TileBuffer 双写（EA2）、脏矩形（EA6）等在 **工坊示例切片** 中修，不塞进本 MCP 剖面 PR。

## 8. 证据与代理计量

### 8.1 证据（EQ4）

`workshop` / `verify` 示例的 smoke 必须断言至少一条玩法路径（战斗回合 / 对话节点 / 画刷写回），禁止「进程存活即 PASS」。

### 8.2 Token 代理（C3，尽力）

`eve_agent_session_status` 增加只读计数（非计费真理）：

- `toolCalls`、`toolsFiltered`、`profile`、`escalateCount`
- `approxResultBytes`（工具返回体字节和）

用于剖面 A/B 对比；不声称等于 Cursor 账单。

## 9. 明确不做

| 项 | 原因 |
| --- | --- |
| Godot 式 `enable_toolset` + 29 类集合 | 开闸成本与 C1 叠加，已证实是 UX 杀手 |
| 换 JS/Lua 作主脚本 | 见 `AI知识补偿.md` |
| 修复 Cursor 客户端 list 缓存 | 非本仓库可控；用 §4.3 双轨规避 |
| 本方案内重做 VN UI / tileset 贴图 / equip 热键 | 成品质量缺口，另开 slice |
| 把整本 API 手册塞进 skill | 与少 token 目标相反 |

## 10. 落地顺序（PR 切片）

### Slice A — 剖面合同（本方案主 PR 序列之首）

1. `eve_agent_session_start` 接受 `taskKind` / `profile`；默认窄剖面。  
2. `McpToolFilter` 支持会话叠加；调用拒绝带 `escalate`。  
3. `eve-mcp --profile=`；文档与 `llms.txt` / evescript skill 各加一小节。  
4. 单测：剖面隐藏、拒绝、env∩session、默认非 all。  
5. （可选）`tools/list_changed` 通知。

### Slice B — 合同真理 + 主线程 defer

1. Binding 生成/注解修复（§6.1）。  
2. MCP 重活入队（§6.4）。  
3. 回归：`devtools` / script-api MCP 测试 + 一条「查合同写 dialogue」用例。

### Slice C — Gameplay 域补齐 + observe 路径

1. rpg/battle/quest `publishGameplay`（§6.2）。  
2. `game.agent.json` 路径统一（§6.3）。  
3. 示例 smoke 断言玩法路径（§8.1）。

### Slice D — Recipe + workshop facade

1. `eve_recipe_*`（§5）。  
2. `eve_workshop` 窄 op（§7）。  
3. 白盒：一句话 intent → recipe.apply → verify.play → session complete（证据门控）。

### Slice E — 工坊质量债（可并行，非门禁）

EA2/EA5/EA6/EQ* 按 FINDINGS 另开 PR；不得阻断 A–D。

## 11. 验收标准

1. **开闸零轮次**：在 `EVE_MCP_TOOLSET` 或 `--profile=verify.play` 下，Agent 首轮即可 `eve_play`，无需 enable。  
2. **越权失败可懂**：错误剖面调 `eve_scene_batch` 得到 `filtered` + 合法 escalate，无 silent success。  
3. **C1 不致命**：故意不刷新 list 时，轨 B 仍拒绝越权；facade 名始终可调。  
4. **少 token 搭景**：`content.scene` 下完成「月下废墟三棵树」仅用 recipe/batch，不生成完整 prop 脚本。  
5. **合同可查**：§6.1 所列漂移项在 `eve_api_get` 与运行时行为一致。  
6. **证据门控**：workshop/verify 示例无「存活即过」；required criterion 无 `eve_eval`。  
7. **`make check`** 通过；相关 MCP 单测绿。

## 12. 架构规范对照（交付时填写）

| 规则 | 本方案如何遵守 |
| --- | --- |
| 模块边界 / 不新开顶层 atelier 包 | facade 在 `devtools` + 现有宿主模块 |
| Result / 不丢弃 | 剖面拒绝与 recipe.apply 用结构化 Result JSON |
| 可选依赖可测 | provider 缺失时 workshop op 返回明确 Unsupported |
| 热路径无字符串乱查 | recipe id → 注册表；非每帧 |
| 新债务标记 | 无；迁移期 deprecated 路径写 owner/expiry |
| 例外 | 无静默豁免；若 Cursor 将来支持 list 刷新，轨 C 可升级为推荐但不改轨 B |

## 13. 成功后的 Agent 默认剧本

```text
session_start(taskKind=content, profile=content.scene, criteria=[layout, visual?])
  → eve_recipe_apply(...)
  → session_advance(verify) 前先 escalate 或新 session(taskKind=verify)
session_start(taskKind=verify, profile=verify.play, criteria=[reachable, dialogue])
  → eve_play / eve_gameplay observe
  → evidence → complete
```

对话里几乎没有全量工具表，也没有 Godot 式 enable 往返。
