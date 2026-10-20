# RPG Atelier

把角色、战斗、背包、对话、**地图编辑**、**场景编辑**放进同一个可运行工程，用来验证
「复杂 RPG 业务 + 内容编辑」在 EVEngine 上的集成体验，并记录缺口。

互补示例：玩法纵向切片见 [`rpg-classic`](../rpg-classic/)，动作 RPG 见 [`rpg`](../rpg/)，
纯 scene_director 舞台见 [`ai-stage`](../ai-stage/)，瓦片笔刷协议见 [`editor-api-v2`](../editor-api-v2/)。

## 运行

```sh
make run/linux-debug GAME=examples/rpg-atelier
# 或
build/linux-debug/src/engine/eve run --debug --mcp-port=7529 examples/rpg-atelier
```

## 模式

| 键 | 模式 | 能力 |
| --- | --- | --- |
| `F1` | Explore | WASD 移动、`E` 对话/开战、`Q` 喝药、`C` 状态、`R` 重置 |
| `F2` | Battle | `1` 斩击 / `2` 火球 / `3` 治疗 / `Esc` 逃 |
| `F3` | MapEdit | 左键画地、右键画墙、中键擦除、`Z`/`Y` 撤销重做（`levelEditor` Brush+History） |
| `F4` | Scene | `scene_director`：`1` 箱 / `2` 柱 / `3` 相机 / `R` 重置舞台 |

## 模块面

- `eve.RPG()`：属性 / 技能注册 / 简易战斗结算
- `eve.Inventory()`：物品 JSON、背包、装备槽，并 `publishGameplay` 给 `eve_gameplay`
- `dialogueFlow` + `dialogue`：`.dnut` 长者对话与史莱姆提示
- `eve.Map()`：`loadFromFileWithObjectContract` 加载 `data/atelier-map.json`
- `levelEditor`：`TileBuffer` + `Brush` + `EditorHistory` 同步回 `TileLayer`
- `scene_director`：与 `ai-stage` 相同的搭台 kit（MCP `eve_scene_*` 亦可驱动）

脚本还导出 MCP 工具 `atelier_status` / `atelier_set_mode`（需 `--debug --mcp-port`）。

## 数据

| 文件 | 用途 |
| --- | --- |
| `data/items.json` | 药水 / 铁剑 / 任务信物 |
| `data/skills.json` / `effects.json` | 技能与效果模板 |
| `data/atelier-dialogue.dnut` | 长者与史莱姆提示对话 |
| `data/atelier-map.json` | 16×10 探索地图 + NPC / 遭遇 |
| `data/world-object-contract.json` | 地图对象契约 |

## 探测记录

见同目录 [`FINDINGS.md`](FINDINGS.md)：本次实现与联调中碰到的架构 / API / 构建 / 体验问题清单。
