# RPG 示例 —— 经典回合制 · 完整 RPG 界面

用 `eve.RPG()` + `eve.Inventory()` 搭的 RPG Maker 风格回合制 demo，演示数据驱动
主模型 + 完整外围界面：**背包 / 商店 / 属性加点 / 装备安装 / 每 3 关调整一次 /
状态界面随时进入**。与之互补的实时动作 demo 见 [`examples/rpg`](../rpg/)。

## 运行

```sh
make run/win32-debug GAME=examples/rpg-classic
# 或对应平台： run/linux-debug / run/macosx-debug
```

## 玩法

**战斗**
- `1` 攻击（`a.attack - b.defense`，可暴击）
- `2` 火球（`a.attack * 2`，火元素，耗 10 MP）
- `3` 治疗
- `Q` 使用背包里的治疗药水
- `C` 随时打开「状态界面」（查看属性 / 装备 / 背包）
- `R` 倒下后重开

**调整界面（每 3 关自动进入一次）**
- **属性加点**：每级获得 3 点，分配攻击 / 防御 / 生命上限 / 魔力上限
- **商店**：用金币购买 治疗药水 / 铁剑 / 皮甲（`data/items.json` 定义物品）
- **装备安装**：把背包里的铁剑 / 皮甲装备到武器 / 护甲槽位，属性加成落到角色

## 数据文件（全部数据驱动，改 JSON 即热重载）

| 文件 | 内容 |
|------|------|
| `data/classes.json` | 职业「战士」：职业基础特征 + 按等级学技能 |
| `data/traits.json` | 特征：力量（攻击 +20% / 暴击 +10%）、火抗（火伤 -50%） |
| `data/skills.json` | 技能定义 |
| `data/items.json` | 物品（背包/商店用）：药水、铁剑、皮甲 |
| `data/quests.json` | 任务「清剿史莱姆」（杀 3 只 → 领 50 金币） |
| `data/effects.json` | 效果模板 |

伤害公式（`"a.attack * 2"`）、暴击、元素、命中在 `main.nut` 用 `registerSkillDamage`
注册，装备属性加成用 `registerItemStatsFromJson` + `syncEquipModifiers`。

## 演示了什么

| 系统 | 用到的点 |
|------|----------|
| 职业 | `setClass` 施加职业特征并学到 1 级技能；升级后 `checkLevelSkills` 补学 |
| 特征 | `paramRate` 落属性、`exParam` 暴击、`elementRate` 火抗 |
| 回合制战斗 | `Battle`：行动顺序、命中/暴击/元素、胜败 `isVictory`/`getWinnerSide` |
| 成长 | `gainXp` 升级、`getLevel`/`getXp`/`getXpToNext` |
| 任务 | `newTracker` + `activate`/`notify`/`claim` |
| 全局状态 | `GameState` 变量记录金币 |
| 背包/商店 | `inventory` 模块：`newBag`/`addItem`/`findItem`/`removeAt`/`canAddItem` |
| 装备 | `newEquipmentSet`/`equipFromBag`/`unequipToBag` + `syncEquipModifiers` |
| 属性加点 | 每级点数，`modifyBaseAttribute` 分配 |
| 状态界面 | `ui` 多窗口 + `setHostVisible` 切换 |