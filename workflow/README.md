# Workflow

面向流派的内容装配**不再放在引擎仓库里**。

引擎只提供可组合能力（`eve run` / `eve mcp` / Schema / EditorHost）。具体游戏的目录约定、schema、共享库、编辑器和 `content/` 真值树，各自作为独立项目维护，这样美术与关卡资源不会撑大引擎 git 历史，引擎 C++ 修复仍然走 `dev` 上的 PR。

## 当前项目

| 项目 | 流派 | 仓库 |
|---|---|---|
| rpg-classic | 经典斜 45° 回合制 RPG | https://github.com/EVEngine/rpg-classic |

本地与引擎联调（Windows 上常见布局：`Agents/EVEngine` 与 `Agents/rpg-classic` 同级）：

```bash
git clone git@github.com:EVEngine/rpg-classic.git
cd rpg-classic
# SDK：eve 在 PATH 上
eve run .
# 或用刚编出来的引擎
EVE_BIN=/path/to/eve eve run .
```

需要改 C++ 时，在 **EVEngine** 仓库对 `dev` 开 PR；不要把引擎补丁提交进游戏仓库。游戏侧约定见该仓库的 `ENGINE.md`。

## 再做一个新的 workflow 项目时

复制 rpg-classic 的**契约**，不要复制它的实现：

1. 独立仓库根目录放 `workflow.json`（`schema: "eve.workflow"`、`version: 1`、稳定 `id`、kind 表、命令表）；
2. `schema/` 里每个 kind 一份 Eve Schema v1；
3. `lib/` 至少覆盖：严格 JSON、Result/诊断、项目索引、存储后端、校验门；
4. 编辑器 View + ViewModel，用宿主 `register(id, drawer)`，不要自己定义 `eve_host_update` / `eve_host_render`；
5. `tools/` CLI 行为与 `rpgc.py` 对齐：校验、暂存、导出、状态。

引擎能力说明：

- [AI 与 MCP](../docs/dev/AI与MCP支持.md)
- [`examples/ai-editor`](../examples/ai-editor/README.md)
- [editor-tool-protocol](../docs/dev/editor-tool-protocol.md)
