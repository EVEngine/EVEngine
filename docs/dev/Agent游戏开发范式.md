# Agent 调用 EVEngine 开发游戏范式

状态：v1 已落地。目标是让 Agent 不只“生成代码”，而是在同一套可审计协议中发现能力、修改游戏、
运行真实引擎、观察结果、验证验收条件，并在失败时恢复后继续迭代。

## 标准闭环

```text
Discover -> Modify -> Run -> Observe -> Verify -> Complete
    |          |       |        |         |
    +----------+-------+--------+-------> Recover -> Modify / Run
                                      Verify -> Modify（未收敛时继续迭代）
```

- **Discover**：先读游戏根目录的 `game.agent.json`（入口、观察面、合法动作、截图后端），再枚举 MCP tools、
  Editor commands、模块能力和当前 runtime 状态。没有契约时 `eve_play` 仍可 `status`/`clock`，但 `observe` 会返回 `Unsupported`。
- **Modify**：编辑 `config.nut/main.nut`、资源或 Editor target；副作用通过明确的文件保存、热重载或
  Editor transaction 发生。
- **Run**：启动/继续真实帧循环，或执行一个确定性 smoke/update 路径。
- **Observe**：用 runtime query、RX observation、UI semantic tree、截图或视觉模型读取权威结果。
- **Verify**：把运行时、测试、截图、checkpoint 或 artifact 证据关联到开始时声明的验收条件。
- **Recover**：编译/运行失败、stale handle、provider 缺失或结果未收敛时，恢复 snapshot、撤销事务、
  修复文件或重新发现 identity，再进入 Modify/Run。
- **Complete**：仅当所有 required criterion 都有 passing evidence，且当前阶段是 Verify 时允许完成。

## 会话协议

Agent 首先调用 `eve_agent_session_start`，传入 objective 和稳定 criterion id：

```json
{
  "objective": "实现并验证可玩的冲刺技能",
  "criteria": [
    {"id": "gameplay", "description": "玩家可触发冲刺且冷却生效"},
    {"id": "visual", "description": "冲刺反馈在截图中清晰可见"},
    {"id": "recovery", "description": "快照恢复后状态确定", "required": false}
  ]
}
```

会话返回 `schema: eve.agent-development-session`、`schemaVersion: 1` 和进程内 `sessionId`。
`eve_agent_session_advance` 强制阶段图；跳过 Run/Observe 直接宣称 Verify 会返回
`invalid-transition`。`eve_agent_session_evidence` 追加带单调 sequence 的不可变 receipt；后续 fail
会撤销同一 criterion 的通过状态，新的 pass 可再次满足它。`manual` 可保存设计判断或待办，但不会
单独把 criterion 标为 passed；required 条件必须有 runtime-observation、test、screenshot、checkpoint
或 artifact 证据。`eve_agent_session_complete` 不接受
“口头完成”：阶段不对或 required criterion 未通过时分别返回 `invalid-phase` / `acceptance-incomplete`。
无法继续时用 `eve_agent_session_abort` 留下明确原因，而不是让会话悬空。

## 权威状态与证据

Development Session 只拥有 objective、phase、criteria 和 evidence metadata，不拥有游戏状态：

- Squirrel/ECS/SceneHost/Renderable3D 仍是运行时权威状态。
- Editor document/target 与 transaction history 仍由 Editor 唯一拥有。
- RX observation 只投影并去重 live runtime 采样。
- Snapshot、测试输出、截图和 artifact 仍由各自产生系统拥有；ledger 保存摘要与 artifact path。

因此证据账本不会形成第二份场景、材质或玩法状态。当前会话是进程内、主线程、无回调协议；host
停止后身份失效。持久化到磁盘、CI 签名证据以及工具结果自动关联属于后续 schema 版本。

## 两条已验证路径

- `examples/ai-game/agent_demo.py`：真实游戏 attach → 状态修改 → runtime 观察 → snapshot 破坏/恢复 →
  可选截图 → evidence-gated complete。
- `examples/ai-editor/editor_demo.py`：生成项目专属编辑器 → Editor transaction → RX 观察收敛 →
  undo/redo → 引擎截图 → evidence-gated complete。

新增游戏示例应至少声明一个玩法/runtime criterion，并执行一个真实 frame/update 路径；compile-only
不能作为“游戏可玩”的证据。涉及视觉结果时应保存引擎自身 readback 截图，不使用桌面/Xvfb framebuffer。

## 下一阶段

1. 让 MCP 工具自动生成带 tool/result correlation 的可信 evidence receipt，减少 Agent 手工登记。
2. 增加 `game.agent.json` 项目契约：入口、smoke 场景、可观察状态、输入动作、容差和发布目标（P0 已在 `examples/ai-game` 落地；Play Host 见 [`2026-09-05-play-verify-evolve.md`](2026-09-05-play-verify-evolve.md)）。
3. 建立可重放 playtest action trace，将 semantic input、simulation seed、checkpoint 和 observation 组合。
4. 把同一会话摘要输出给 CI/PR，明确区分通过、未运行、环境阻塞和视觉待审。
