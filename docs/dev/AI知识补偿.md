# EVEngine 对 AI 的知识补偿

状态：已落地最小闭环（短文 + Binding Contract JSON + MCP 查询 + Cursor skill）。
相关：[`AI与MCP支持.md`](AI与MCP支持.md)、[`Agent游戏开发范式.md`](Agent游戏开发范式.md)、
[`2026-08-29-agent-native-engine-architecture.md`](2026-08-29-agent-native-engine-architecture.md)。

## 判断

**调用（runtime）已经方便。写代码（authoring）还不方便。**

引擎已有嵌入式 MCP、Play Host（`eve_play` + `game.agent.json`）、证据会话、引擎自有截图、
EditorHost 热重载、LSP Binding Contracts。Agent 不需要点 GUI，也不该靠 `eve_eval` 改血量。

Unity / Godot 赢在预训练：C#、Node、Scene、Prefab、GDScript 在语料里海量出现。
EveScript（Squirrel 超集）和 `eve.X()` / `persist` / Result 表几乎不在语料里。
这个缺口不会被更多手册填上，也不会被换成同样小众的 Lua 绑定填上。

正确目标：不要跟 Unity 比「模型是否背过 API」；要比「模型第一次接触时，能否发现正确 API、
写错后能否被编译器/运行时当场纠正、内容修改能否不写脚本」。

## 不要做的两件事

- **不要把「换成 JS/Lua/C#」当主方案。** EveScript 是唯一游戏脚本入口。换语言会丢掉现有绑定、
  Result 契约、热重载和 MCP eval；JS 绑定一旦不够像浏览器/Node，模型照样胡编。
  EveScript 已有 import/export、渐进类型、命名参数、async，对模型比纯 Squirrel 更接近 TypeScript。
- **不要把 MCP 工具清单当引擎说明书。** 工具太多会挤掉真正该注入的语言与规范。缺口是必须学会的合同，
  不是更多平行工具。内容面走结构化文档 + 领域命令；脚本面走 Binding Contract。

## 四件事同时成立才叫对 AI 好用

1. **可发现**：不问人就能列出「这个构建里脚本能调什么」。
2. **可类比**：用 Unity/Godot 概念当索引，而不是当实现。
3. **可约束**：内容走 schema/命令；脚本走 Binding Contract + 编译诊断。
4. **可证伪**：写完必须跑真实帧、截图、observe，不能口头完成。

## 三层产品

### 层 A — 推理时知识包

给 Coding Agent 的默认上下文必须短、稳定、可版本化，不要把整本
[`docs/usr/EVESCRIPT.md`](../usr/EVESCRIPT.md) 塞进 system prompt。

入口：引擎仓库 [`.cursor/skills/evescript/SKILL.md`](../../.cursor/skills/evescript/SKILL.md)
与根目录 [`llms.txt`](../../llms.txt)。**SDK 发行物**把同一份 skill 装到
`share/eve/ai/SKILL.md` 和 `.cursor/skills/evescript/SKILL.md`，并把 `llms.txt` 放在 SDK 根目录，
这样解压 `eve-sdk-*.zip` 的游戏开发者不必克隆源码。

### 层 B — 机器可读 API 目录

真相来源是 `expose(...).addFunc`，不是 Markdown「API 快查」。

| 产物 | 命令 / 工具 |
| --- | --- |
| JSON 目录 | `python3 scripts/generate_binding_contracts.py --json-output eve-api.json` |
| 伪 TypeScript 签名（不是第二语言） | 同上，加 `--dts-output eve-api.d.ts` |
| SDK 安装 | `share/eve/ai/eve-api.json` 与 `eve-api.d.ts`（与该 SDK 的 `bin/eve` 同源） |
| 运行时查询 | MCP `eve_api_search` / `eve_api_get`（读本构建生成的 Binding Contracts） |

构建时 CMake 会在 `EVCommon` 二进制目录写出 `eve-api.json` / `eve-api.d.ts`，
`cmake --install` 把它们拷进 SDK。手册继续给人读；Agent 默认先查合同再写脚本。

### 层 C — 少写脚本

AI 改「有 schema 的事实」；脚本只写无法数据化的行为。

- 场景 / 材质 / PCG / UI：文档 + 领域命令（EditorHost / scene_director）。
- 剧情 / RPG 步骤：`.dnut` 保持数据方言，不要变成第二种通用语言。
- 玩法观察 / 操作：`game.agent.json` + `eve_play`。**禁止把 `eve_eval` 教成过关手段。**
- 生成脚本后的闭环：查 API → 编译诊断 → 真实帧 → `eve_screenshot` / observe → 证据会话。

## 编译器当老师

绑定合同已进入 LSP hover/补全。MCP `eve_api_search` 使用同一份合同，避免 Agent 对着 40 个模块手册猜。
静态扫生成的 `.nut`、金样例反例测试、合成微调语料都后置：等目录稳定后再做。现在微调只会记住过期 API。

## 成功标准

一个没见过本仓库的 Agent，只靠 **SDK 内** skill + `share/eve/ai/eve-api.json` 或 `eve_api_search`，
能写出通过编译的 `eve_update` 移动方块，并且用 MCP 截图或 observe 证明，而不是从 Unity C# 臆造 API。
