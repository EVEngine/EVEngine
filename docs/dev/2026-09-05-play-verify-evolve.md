# Agent Play / Verify / Evolve 合同

日期：2026-09-05
状态：设计提案，尚未代表代码已实现
范围：把 VibeGame 同构的「可操作工程 + 交互验证 + 经验复用」收成 EVEngine 自己的三层合同
配套：[`2026-08-29-agent-native-engine-architecture.md`](2026-08-29-agent-native-engine-architecture.md)、[`Agent游戏开发范式.md`](Agent游戏开发范式.md)、[`玩家与AI共用游戏控制API.md`](玩家与AI共用游戏控制API.md)、[`AI与MCP支持.md`](AI与MCP支持.md)

## 1. 结论

不移植 VibeGame 的八角色 Agent 团队，也不把 Phaser JSON 场景树搬进 EVEngine。

EVEngine 已经选对了更硬的内核：权威文档、领域命令、玩家/AI 共用 `GameplayControl`、证据门控的 `eve_agent_session_*`、引擎自有截图。缺口是 **Agent 面前仍是一堆平行 MCP 工具**，没有一条它必须学会的 Play 合同，因此验证会滑回 `eve_eval` / 改脚本 / 口头 Complete。

本设计只增加三层、不新增 `agent` 模块：

```text
game.agent.json          项目声明：怎么玩、观察什么、验收什么
        |
   Play Host             统一 pause / 推进入口 / 语义动作 / snapshot / 截图
        |
  Play Trace             可回放的语义操作账本，绑定 session evidence
        |
 Prior Promotion         验收过的示例晋升为骨架 / 自检模块 / 协作合同
```

默认主路径：

```text
读取 game.agent.json
  -> 提交领域命令或脚本修改
  -> Play Host 在独立实例里推进并观察
  -> 写出 Play Trace + 引擎截图
  -> session 用证据关门
  -> （可选）人工确认后晋升 prior
```

## 2. 与 VibeGame 的对齐方式

| VibeGame | EVEngine 对应物 | 本设计怎么收口 |
| --- | --- | --- |
| 文本场景/节点 + schema | 领域 Document + `editing` target，不是统一 GameObject JSON | 不另造场景 IR；Play 只读运行时投影 |
| `vibegame play continue/snapshot/input` | `eve_pause` / `eve_step_*`、`eve_gameplay`、`eve_screenshot`、`eve_snapshot_*` 分散 | 合成 **一个** `evengine.play-request` |
| auditor / player / reviewer | `eve_agent_session_*` 阶段图 + criterion | Trace 自动登记 evidence，禁止 eval 充当 required 证据 |
| git worktree 并行 | Agent-Native 独立工作区 | 本版不改；Play 实例绑定工作区根与端口 |
| skeleton / module / contract | `examples/`、领域 satellite、docs 合同 | 晋升清单，禁止静默写回 `src/` |
| 八角色 tmux 团队 | Cursor / Claude Code 作为 harness | 引擎不调度 LLM；角色提示若需要，只消费 Play Host |

VibeGame 把「整局 Phaser 时钟」和「玩法推进」合成一次 `continue N frames`。EVEngine 必须拆开，否则战术回合、RPG 结算和渲染帧会被假装成同一种时间。

## 3. 两条时钟，禁止混用

| 时钟 | Owner | 用途 | 非法用途 |
| --- | --- | --- | --- |
| **Host clock** | 引擎主循环 / Debugger | 推进一整帧：脚本 `eve_update`、物理、UI、呈现 | 当作某个玩法域的权威回合结算 |
| **Domain clock** | `IGameplayControlProvider::advanceGameplay` | 注入 `SimulationStep`，推进**一个**玩法实例 | 用来「顺便」刷新整屏或绕过未接入域 |

Play Host 的 `step` 必须显式选择 `kind`：

- `frame`：等价于现有 `eve_step_frame` / 连续 N 帧；用于 `examples/ai-game` 这类整局脚本游戏。
- `gameplay`：必须带 `domain` + `instance`；用于已接入 `GameplayControl` 的 tactics / rts / rpg.battle 等。
- 禁止 `eval("for i=0; i<N; i++ eve_update()")` 作为推进合同。

暂停语义：Host clock 暂停时，Domain clock 的 `advanceGameplay` 仍可在**同一主线程**被 Play Host 调用（TestDriver）。这与「玩家在暂停菜单里游戏继续跑」不是一回事；TestDriver 访问必须写进 `GameplaySession.access`。

## 4. 项目契约：`game.agent.json`

每个要对 Agent 可验证的游戏目录在根放置一份契约。它不是第二份 GDD，也不拥有 Scene。它只声明 **入口、观察面、合法动作来源、证据容差**。

```json
{
  "schemaId": "evengine.game-agent-contract",
  "schemaVersion": 1,
  "id": "examples/ai-game",
  "entry": "main.nut",
  "smoke": {
    "scene": "default",
    "seed": 42,
    "maxHostFrames": 180,
    "mustReach": ["combat-alive"]
  },
  "clock": { "defaultStep": "frame" },
  "observations": [
    {
      "id": "combat-alive",
      "kind": "script-root",
      "path": "gameState",
      "fields": ["tick", "player.hp", "enemy.hp"]
    }
  ],
  "actions": {
    "source": "script-map",
    "map": [
      { "id": "reset", "script": "game.reset()" }
    ]
  },
  "capture": { "requiredFor": ["visual"], "backend": "engine-readback" },
  "tolerances": {
    "screenshot": "human-or-vlm",
    "numericAbs": 1e-4
  }
}
```

规则：

- 未知根字段失败，不静默忽略（与 `GameplayControlJson` 相同）。
- `observations.kind` 第一版只允许：`gameplay-domain`（走 `observeGameplay`）、`script-root`（走已 `markStateRoot` 的快照投影，**不是任意 eval**）、`editor-target`（走 editing 查询）。
- `actions.source` 为 `gameplay-domain` 时，动作集合必须来自 `availableGameplayActions`；脚本游戏可用 `script-map`，但每条动作仍要有稳定 `id` 和 Result 回执，禁止开放 `eve_run_script` 当主路径。
- `capture.backend` 只能是引擎 `IRenderCapture`。Xvfb / 桌面 framebuffer 不是合法证据。
- 没有这份文件的项目，Play Host 仍可做 `status/clock`，但 `observe/act` 返回 `Unsupported`，避免 Agent 对任意工程猜测 API。

第一批 consumer：`examples/ai-game`、`examples/ai-editor`。领域编辑器示例（Action / Animation Clip 等）只有在声明 `observations.kind = editor-target` 后才算 Agent-playable。

## 5. Play Host

### 5.1 位置

放在 **DevTools**，与 `executeGameplayControlRequest` 同构：

- C++：`eve::dev::executePlayRequest(const Value&)`
- JSON：`schemaId = evengine.play-request`，`schemaVersion = 1`
- MCP：一个工具 `eve_play`，参数就是该请求对象
- Squirrel：`eve.dev.play(requestTable)` 只做 JSON/Value 门面

不新建模块，不让 `common` 吸收 Playwright 式浏览器控制。Release / 无 DevTools 的裁剪构建：MCP 工具不存在，capability 查询为缺失，结果是 `Unsupported`。

### 5.2 请求形状

```json
{
  "schemaId": "evengine.play-request",
  "schemaVersion": 1,
  "op": "step",
  "clock": "frame",
  "count": 10,
  "trace": "append"
}
```

`op` 封闭枚举：

| op | 含义 | 权威系统 |
| --- | --- | --- |
| `status` | 暂停否、当前 host frame、活跃 gameplay domains、契约摘要 | Debugger + cap 查询 + `game.agent.json` |
| `clock` | `pause` / `play` | Debugger，与现有 `eve_pause` / `eve_continue` 同一条路径 |
| `step` | 推进 `count` 个 `frame` 或一次 `gameplay` | Host loop 或 `advanceGameplay` |
| `observe` | 按契约 id 取 owning 快照 | GameplayControl / marked script root / editing query |
| `act` | 提交一条契约内动作 | `submitGameplay` 或 script-map 包装函数 |
| `capture` | 引擎 readback PNG | `IRenderCapture` |
| `checkpoint` | `capture` / `restore` | 现有 snapshot，只含契约声明的 state root |
| `batch` | 顺序执行子请求，首次失败停止 | Play Host |

`trace` 可为 `off` / `append`。默认：存在活跃 `eve_agent_session` 且阶段为 `run`/`observe`/`verify` 时为 `append`。

### 5.3 动作与观察禁止事项

- **禁止**把 `eve_eval` / `eve_run_script` 标成 Play 主路径。它们保留为调试逃生舱。
- **禁止** `act` 直接改 HP 等权威字段，除非契约把该动作标为 `DeveloperCheat` 且 session access 匹配。`examples/ai-game` 现有 `game.setEnemyHp` 是演示作弊，不得作为 required `gameplay` criterion 的唯一证据。
- **禁止** Play Host 在持锁时调用未知脚本回调或 LLM。
- `act` / `step` / `checkpoint` 一律 `[[nodiscard]] Result`；失败码稳定：`Unsupported`、`NotFound`、`StaleHandle`、`Rejected`、`Conflict`、`Failed`。
- 返回值是 owning `Value`。不得返回指向场景节点或 VM 栈的裸指针。

### 5.4 与现有 MCP 的关系

| 旧工具 | 第一版 | 之后 |
| --- | --- | --- |
| `eve_play` | 新合同，Agent 文档只教这一条验证路径 | 保持 |
| `eve_gameplay` | 仍是玩法域 JSON 门面；`eve_play` 内部调用它 | 不删除 |
| `eve_pause` / `eve_step_frame` / `eve_screenshot` / `eve_snapshot_*` | 内部复用 | 文档降为「底层」 |
| `eve_eval` | 调试 | required criterion 不接受 `kind=eval` |

`examples/ai-game/README.md` 与 `agent_demo.py` 在 P0 结束后改走 `eve_play`。

## 6. Play Trace

### 6.1 为什么 ScenarioRecorder 不够

`ScenarioRecorder` 记录平台事件队列（键鼠），用于复现「某一帧吃进了什么输入」。它不是玩法语义，也不能证明「冲刺冷却生效」。

Play Trace 记录的是 **Play Host 操作**，与玩家语义命令同一层。

### 6.2 格式

```json
{
  "schemaId": "evengine.play-trace",
  "schemaVersion": 1,
  "contractId": "examples/ai-game",
  "contractHash": "<sha256 of game.agent.json>",
  "engine": { "product": "EVEngine", "revision": "<git or sdk>" },
  "seed": 42,
  "startCheckpoint": "artifacts/play/ckpt-001.json",
  "steps": [
    {
      "seq": 1,
      "op": "act",
      "action": "jump",
      "hostFrame": 12,
      "tick": 12,
      "receiptId": "cmd-1",
      "observationDigest": "sha256:...",
      "capture": "artifacts/play/jump.png"
    }
  ],
  "result": { "status": "passed", "failedCriterion": null }
}
```

- 未知根字段失败。
- `observationDigest` 只哈希契约声明的字段，避免把粒子随机数编进失败。
- GPU 画面不进 hash。视觉 criterion 的证据是 `capture` 路径 + 可选 VLM 摘要；判定分为 `passed` / `needs-review` / `failed`。`needs-review` 不能让 required visual 自动通过。
- 回放：restore `startCheckpoint` → 按 `steps` 重放 → 数值 digest 在 `tolerances` 内必须相等；截图不要求逐像素相等。

存储：工作区 `.eve/play/<sessionId>/trace.json`。session 的 evidence `kind` 使用 `play-trace`、`runtime-observation`、`screenshot`、`checkpoint`。`manual` 仍不能单独通过 required 条件。

### 6.3 自动 evidence

当 `eve_agent_session` 活跃时，成功的 `observe` / `capture` / `checkpoint` / 完整 `batch` 由 Play Host **同步**追加一条 receipt（单调 sequence）。Agent 不必再手写 `eve_agent_session_evidence` 才能关门；它仍可追加 `manual` 说明。

失败的 Play 操作不把 criterion 标为 passed。随后同 criterion 的 fail 会撤销先前 pass（沿用现有 ledger 规则）。

## 7. 验证角色，但不做引擎内 Agent 进程

VibeGame 的 auditor / player / reviewer 对应 **证据种类**，不是 tmux 角色：

| 角色（论文） | EVEngine 由什么扮演 |
| --- | --- |
| auditor | 领域命令 Result、editing validation、`vibegame check` 的对应物：现有测试 + document schema |
| player | Play Host + Play Trace |
| reviewer | `eve_agent_session_complete`：Verify 阶段且 required 全过 |
| orchestrator | 外部 coding agent；引擎只提供 Discover（契约 + MCP tools） |

若未来要写仓库内提示词（`agents/player.md`），它们只能调用 `eve_play` 与 Editor 命令，禁止教 `eve_eval` 改血量过关。本设计不把提示词列入引擎交付。

## 8. Prior Promotion（Evolve）

VibeGame 的污染风险成立：错误经验写进源码会伤害后续项目。EVEngine 更危险，因为晋升对象可能是 C++ 模块。

### 8.1 三种 prior，按你们已有分层命名

| 种类 | 是什么 | 放哪 | 晋升前必须 |
| --- | --- | --- | --- |
| **skeleton** | 可运行示例目录：脚本 + `game.agent.json` + 至少一条 passed Play Trace | `examples/<name>/` | `make test` 过滤该例；Play Trace 回放 |
| **module** | 可裁剪运行时或 `*_editing` satellite，带契约测试 | `src/modules/` | 现有模块清单、depgraph、provider 在/缺测试 |
| **contract** | 某一类功能的协作 SOP（谁改 Document、谁跑哪条 Play、合并时跑什么） | `docs/dev/contracts/` 或模块旁 `*.contract.md` | 至少两个真实 consumer 引用 |

不是把游戏美术拷进引擎。skeleton 去掉项目私货的规则与 VibeGame 相同：保留结构、数值比例、验收动作；资源用占位引用。

### 8.2 晋升是评审步骤，不是隐式训练

第一版 **不做** `eve evolve` 自动改 `src/`。清单即可：

1. 候选目录通过 Play Trace 回放与相关单测。
2. 文本中无本机绝对路径。
3. 用户或维护者确认 GDD/契约仍正确。
4. 以普通 PR 进入 `examples/` 或模块树；CI 跑 `ARCHITECTURE_BASE` 合同门。

以后若做 CLI，也必须：dry-run、拒绝覆盖已有 prior、skeleton 先独立进程冒烟、索引手改。不得在 Agent 会话 Complete 时自动晋升。

## 9. 与领域编辑器的接缝

当前 Action / Animation / Audio / Avatar / Voxel / Procgen Workspace 的权威在 C++ Document。Play 在这里的含义是：

- `act` 提交已有 Editor command / `DomainOperation`，不点面板。
- `observe` 读 target revision + preview 快照（例如 `AnimationClipPreview.documentRevision`）。
- `step kind=frame` 只用于预览播放头；正式内容只在事务提交时变。
- 视觉证据仍然是 `IRenderCapture`，过期 preview generation 必须 `Conflict`，不得当 passed 截图。

没有 Workspace 面板的 automation 命令不得假装已交付编辑器（沿用 2026-09-04 Workspace 合同）。Play 不能成为「先注册 command 再补 UI」的后门。

## 10. 分阶段落地

### P0 — Play 门面（最小可教路径） — 已落地

- `evengine.play-request` v1：`status` / `clock` / `step(frame)` / `observe(script-root)` / `capture` / `checkpoint` / `batch`
- MCP `eve_play` + `test/play_host.cpp`（无 `IRenderCapture` 时 `Unsupported`）
- `examples/ai-game/game.agent.json`；`agent_demo.py` 改走 `eve_play`
- 文档：`Agent游戏开发范式.md` 的 Discover 改为先读契约

验收：不教 `eve_eval` 也能完成「暂停 → 观察 → 推进 N 帧 → 截图 → checkpoint 破坏/恢复」。

### P1 — 语义动作 + Trace + 自动 evidence — 已落地

- `act` 接 `script-map` 与至少一个 `gameplay-domain`（复用已有 tactics 或 rpg.battle 测试）
- `evengine.play-trace` 记录与回放
- 活跃 session 下成功 observe/capture 自动 `record`
- required criterion 拒绝 `kind=eval`

验收：回放同一 trace 两次，数值 digest 稳定；Complete 在缺截图时失败。

### P2 — 编辑器 consumer

- `examples/ai-editor` 或 `examples/combat-action-editor` 声明 `editor-target` 观察
- 一条 Play Trace：改时间轴 / clip → 观察 revision 收敛 → 截图

验收：UI 与 `eve_play` 对同一 command 产生同一文档 revision。

### P3 — Prior 清单

- `docs/dev/contracts/` 先写两份：`play-host.md`（本文压缩版给 Agent）、`action-timeline.md`（已有 Workspace 的验收 Play）
- 选一个稳定 example 作为 skeleton 样板，晋升流程写在贡献文档，不做自动写回

## 11. 明确不做什么

- 不建立引擎内多 Agent 调度、tmux、租约中心。
- 不把全部运行时状态变成 JSON 场景树。
- 不把 `eve_eval` 升级为官方观察协议。
- 不要求所有游戏确定到位像素；只要求契约字段可判定。
- 不在无 DevTools 的发布裁剪里偷偷带上 Play Host。
- 不让 Play Trace 冒充 `ScenarioRecorder` 的键鼠复现器。
- 不把 VLM 失败标成引擎 Failed；视觉未审是 `needs-review`。
- 不在 Complete 时自动 `evolve`。

## 12. 测试与架构门

- 新协议走 `test/play_host.cpp`、`test/play_trace.cpp`（过程隔离），不塞进共享 test main。
- 每个 `IGameplayControlProvider` 的现有合同测试保持；Play 只增加「经 Host 调用与直连 JSON 门面等价」一条。
- `game.agent.json` / play-request / play-trace 列入 schema 与未知字段拒绝测试。
- 可选图形：无 `IRenderCapture` 的测试必须看到 `Unsupported`，不能 skip 成通过。
- 落地后：`ARCHITECTURE_BASE=HEAD make check/architecture-contracts`。

## 13. 架构规则适用

- **Result**：Play 操作不得新增 `bool` + lastError。
- **唯一 owner**：Session 只拥有阶段和 evidence 元数据；游戏状态仍在脚本/ECS/玩法域；文档仍在 Editor。
- **短根**：不引入 PlayObject / AgentActor。
- **时间**：Host frame 与 Domain `SimulationTick` 分开记录。
- **线程**：Play 与 GameplayControl 一样，主线程、不重入、不保留请求指针。
- **持久化**：三种 JSON schema 有 id、version、未知字段失败、无部分发布回放。
- **可选依赖**：DevTools / RenderCapture / 某 gameplay provider 缺失均显式 `Unsupported`。
- **模块**：不新增 `agent` 模块；DevTools 组合现有能力。
- **例外**：无。P3 CLI 若以后要做，另开设计，不得静默并入 P0。
