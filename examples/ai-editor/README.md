# AI Editor — 让 AI 现场长出一个项目专属编辑器

演示 EVEngine 无头 MCP 主机（`eve mcp`）的核心能力：**AI 提交一段 JSON View +
Squirrel ViewModel，引擎现场生成一个可交互的编辑器窗口**，并做 MVVM 双向绑定。
每个项目都能长出风格与功能完全不同的地形编辑器、材质编辑器、事件编辑器——
不再受统一 IDE 样式约束。

## 启动（TCP 模式）

```bash
eve mcp --port 7531 --root examples/ai-editor
```

## 一键复现

```bash
python examples/ai-editor/editor_demo.py 7531
```

脚本会走完：附着 → 开窗 → 注册 ViewModel → 提交编辑器 JSON → 渲染数帧 →
查询状态 → 截图 → 验证绑定 → 干净退出，全部通过后输出 `PASS`。

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

然后 Agent 用 `eve_host_editor_apply` / `eve_host_vm_register` /
`eve_host_capture` 等工具现场生成并迭代编辑器（工具全表见
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

> 说明：`terrain.vm.nut` 里的 `apply` 目前只做演示占位；真实项目在这里读写
> 引擎数据（Heightmap / Material / Scene），几行脚本就能把示例变成真正可用的编辑器。
