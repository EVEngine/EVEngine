# AI Editor — 让 AI 现场长出一个项目专属编辑器

演示 EVEngine 无头 MCP 主机（`eve mcp`）的核心能力：**AI 提交一段 JSON View +
Squirrel ViewModel，引擎现场生成一个可交互的编辑器窗口**，并做 MVVM 双向绑定。
每个项目都能长出风格与功能完全不同的地形编辑器、材质编辑器、事件编辑器——
不再受统一 IDE 样式约束。

本示例也是标准 Agent Development Session 的 Editor 路径：objective 与验收条件先登记，随后按
Discover → Modify → Run → Observe → Verify 推进，RX 收敛、undo/redo 和引擎截图作为独立证据后才能完成。

## 启动（TCP 模式）

```bash
eve mcp --port 7531 --root examples/ai-editor
```

## 一键复现

```bash
python examples/ai-editor/editor_demo.py 7531
```

脚本会走完：附着 → 开窗 → 注册 ViewModel → 提交编辑器 JSON → 创建运行中的
`SceneHost` 和真实 `Renderable3D` → 让 Editor live target 借用二者 → 通过
`eve_editor_observe_start/poll/close` 建立跨多次 Agent 调用的 RX 观察会话，再用
`eve_editor_execute_observe` 的 `scene-node` observer 提交并观察命名 SceneHost，缺失节点会在写入前
拒绝；同一工具的默认 `renderable3d` observer 在一次调用中关联 live Renderable3D 的
`before/after`、Editor 事务回执与 target snapshot → 先用错误 generation 证明 stale handle 会在
写入前拒绝且不改变运行时，再用当前 identity 自纠 → 先提交一个偏离目标的
材质候选，由 Agent 从 RX 会话读取变更后的 `converged/maxError`，提交修正并重新观察至收敛；
相同采样被 `distinctUntilChanged` 抑制，会话结束时显式关闭订阅 →
用两次 undo/redo 回放候选与修正 → 把权威结果
投影回 UI → 渲染绿色材质立方体并由引擎截图 → 关闭 target → 干净退出，全部通过后输出 `PASS`。

> 说明：`eve mcp` 主机启动时会实例化全部模块（等价 load.nut 的绑定循环），
> 因此 IEditorHost / IRenderCapture 等能力始终可用；本示例已在 Windows 上
> 端到端验证通过（含 MVVM 双向绑定与窗口截图）。

## 也可以直接接入 Codex / Cursor / Claude

`eve mcp` 无参数时走 stdio（无需 Node 桥），直接在 MCP 配置里指向它：

```json
{ "mcpServers": { "evengine-host": {
    "command": "eve", "args": ["mcp", "--root", "examples/ai-editor"]
} } }
```

然后 Agent 用 `eve_host_editor_apply` / `eve_editor_target_create` /
`eve_editor_observe_start/poll/close` / `eve_editor_execute_observe` / `eve_editor_inspect` /
`eve_host_capture` 等工具现场生成编辑器、
修改引擎 live scene / Renderable3D 材质并从统一 runtime observer 验证结果（工具全表见
[`docs/dev/AI与MCP支持.md`](../../docs/dev/AI与MCP支持.md)）。

## 保存即生效

主机运行期间可直接修改 `editors/terrain.editor.json` 或
`editors/terrain.vm.nut`。`eve mcp` 会自动重载文件，不需要重新编译或重启测试；当前
MCP 连接和编辑器控件值会保留。若新脚本编译失败，旧 ViewModel 继续工作，可通过
`eve_host_hot_reload_status` 查看错误，修复并再次保存即可恢复。

项目还可以添加 `mcp.nut` 或 `mcp/*.nut` 注入专用工具逻辑。Agent 需要确定性刷新时可调用
`eve_host_resource_reload {"path":"editors/terrain.vm.nut"}`。

## 文件

- `editors/terrain.editor.json`：编辑器 View（控件树 + 主题 + 布局）。
- `editors/terrain.vm.nut`：绑定的 ViewModel（Squirrel 表，字段可读可写，
  按钮命令回调 `apply(editor, widget)`）。
- `editor_demo.py`：标准库 TCP 客户端，展示完整驱动流程。

`terrain.vm.nut` 的按钮回调仍只负责 UI 交互；权威 live SceneHost 修改由 Agent 通过统一
Editor command/transaction 协议提交，避免 ViewModel 直接维护第二份场景状态。Editor
关闭 target 时只销毁借用适配器和发布 sink，不销毁 ECS 拥有的 SceneHost/Renderable3D。
