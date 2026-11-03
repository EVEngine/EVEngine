# Action

**脚本入口：** `eve.Action()`（槽位 `action`）

`action` 是玩法行动与 Ability 的协议模块：唯一拥有 `ActionRuntime` 的 phase 状态，以及
`AbilityRuntime` 的 grant / cooldown / activation 链接。它不拥有生命值、棋盘或角色实体。

## Squirrel 快速开始

```squirrel
local action = eve.Action();
local created = action.newRuntime();
assert(created.ok);
local runtime = created.value;
assert(runtime.ownership() == "owned");

local registered = runtime.registerAbilityJson(abilityAssetJson);
assert(registered.ok);
local granted = runtime.grantAbility("fighter:player", "ability:light-attack");
local activated = runtime.activateAbility(granted.value.grantId, 1);
local advanced = runtime.advanceAbilities(2, 0.10);
```

`newRuntime()` 返回脚本 GC 拥有的 `ActionRuntime` 代理；内部同时持有
`ActionRuntime` 与借用它的 `AbilityRuntime`。冷却与 phase 只接受调用者注入的
tick / delta，不读取墙钟。所有易失败方法返回统一 Result 投影。

## 绑定与行为说明

| 方法 | 说明 |
| --- | --- |
| `getName` | 模块名 `"Action"` |
| `newRuntime` | 创建脚本拥有的 `ActionRuntime`（Result + `ownership`） |
| `ownership` | 运行时代理所有权标记，恒为 `"owned"` |
| `registerAbilityJson` / `replaceAbilityJson` | 解码 `eve.action.ability` schema v2 并注册/热替换 |
| `registerTimelineAbility` / `replaceTimelineAbility` | 与 Combat 同形的 timeline 注册入口 |
| `grantAbility` / `revokeAbility` / `abilityGrant` | grant 生命周期与快照 |
| `activateAbility` / `advanceAbilities` / `cancelAbility` | 激活、推进、取消 |
| `matchingAbilities` | 按 gameplay-event tag 匹配 grant |
| `submitAction` / `advanceAction` / `cancelAction` / `findAction` | 不经 Ability 的直接 ActionRuntime 路径 |
| `executionCount` | 当前未终态 execution 数 |

Ability 生命周期仍由本模块的 `AbilityRuntime`/`ActionRuntime` 唯一拥有。Combat /
RPG / Weapon 等域可以继续在 C++ 侧复用同一协议，也可以通过本脚本面独立编排。

## Guide pack（白膜 → AI 成图）

`eve.action.guide-pack` schema v1（`schema` / `schemaVersion` 线格式）描述镜头、AOV 合同与
Ability 绑定草稿。编解码与 CPU 骨骼投影在 `common/ActionGuidePack.h`；MCP 工具
`eve_action_guide_pack_*` / `eve_action_guide_landmarks` / `eve_action_guide_capture` /
`_status` / `_export` 供 Agent 使用。捕获经 `IActionGuideCapture`（缺席 → `ProviderAbsent`）；
Vulkan 图形后端注册 `IActionGuideCapture` 后可离屏产出 silhouette/depth（及皮肤
palette 派生的 bone）；就绪后导出 `manifest.json` + `shots/NNNN/{silhouette,depth,bone}.png`。
`eve_action_guide_ingest` 校验候选帧并写出序列 + `ability.draft.json`（不注册正式 Ability）。设计见
[`docs/dev/2026-11-03-2d-action-ai-guide-pipeline.md`](../../dev/2026-11-03-2d-action-ai-guide-pipeline.md)，
样例 [`docs/dev/fixtures/eve.action.guide-pack.v1.example.json`](../../dev/fixtures/eve.action.guide-pack.v1.example.json)。
