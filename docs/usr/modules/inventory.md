# 背包 / 物品栏模块

**脚本入口：** `eve.Inventory()`

数据驱动的物品定义、固定格容器（背包/箱子）、跨包转移、具名装备栏，以及可插拔的接纳 / 容量 / 堆叠规则。

## 基本用法

```squirrel
local inv = eve.Inventory();
inv.registerItemsFromJson(@"[
  {""id"":""potion.hp"",""displayName"":""治疗药水"",""maxStack"":20,""weight"":0.2,""tags"":[""potion""]},
  {""id"":""sword.iron"",""maxStack"":1,""weight"":3.5,""equipSlot"":""weapon"",""tags"":[""weapon""]}
]");

local bag = inv.newBag(20);
bag.setId("player");
bag.setMaxWeight(50.0);
bag.addItem("potion.hp", 5);
print(bag.countItem("potion.hp") + "\n");
```

## 对象关系与调用时机

`Inventory` 持有进程级物品定义表，并创建 `Bag` / `EquipmentSet`。容量策略名、接纳规则名、堆叠规则名都是字符串；内置规则开箱即用，自定义规则需在 C++ 侧 `InventorySystem::register*`。变更事件可在操作后用 `getChangeEvent*` 轮询。

## 目标导向指南

### 做玩家背包与任务栏

`newBag(n)` 建主背包；另建一个 `acceptTags` 仅含 `"quest"` 的袋子作任务栏。主背包可 `addRejectTag("quest")` 拒绝任务物品误入。

### 穿脱装备

`newEquipmentSet()` 后 `defineSlot("weapon")`，可选 `addSlotAllowedTag`。从背包 `findItem` 得到槽位，再 `equipFromBag` / `unequipToBag`。属性加成请在脚本或 C++ `registerChangeHook` 里对接 `rpg` 模块，背包本身不依赖 RPG。

### 原子保存背包与装备

`eve.InventorySaveSession()` 通过 `bind(bag, equipment)` 借用两个权威容器。
`snapshotJson()` 生成 schema `eve.inventory.save-session`、版本 1 的确定性 JSON；
`restoreSnapshotJson()` 会先校验所有物品定义、策略名、物品实例身份、堆叠上限和装备槽契约，
再一次发布背包与装备状态。任一字段失败时两个容器都不改变，也不会产生库存事件或调用 hook。
物品定义注册表属于内容，不写入玩家存档；读档前必须先加载相同内容版本。

### C++ 批量加入事务

跨系统奖励应使用 `InventorySystem::prepareAddBatch()` 先在私有候选背包上验证完整批次，再用
`commitAddBatch()` 提交。准备阶段不改变背包、不产生轮询事件、也不调用 hook；提交时如果背包自准备后
发生变化，会以结构化 `Conflict` 拒绝。成功提交先一次替换权威槽位，随后才发布每项 `add` 事件，
因此观察者不会看到半批物品。预备对象仅可移动、只能提交一次，并且只能在背包所属模拟线程使用。

需要和货币、任务等其他权威状态共同提交的移除操作使用 `prepareRemove()` / `commitRemove()`；它采用
同样的候选背包、陈旧检测和提交后事件契约。RPG 商店已在此基础上提供购买与出售事务。

## 常见问题

- 未先 `registerItemsFromJson` / C++ `registerItem` 就 `addItem`，会放入 0 个。
- `canAddItem` 表示能否放入**全部**请求数量；`addItem` 允许部分成功。
- 多种物品共享容量时不要逐项 `canAddItem` 后再添加；C++ 使用 `prepareAddBatch` / `commitAddBatch`。
- `maxWeight` / `maxVolume` 为 `<=0` 时该维度不限制（仍受所选 `capacityPolicy` 约束）。

## API 快查

- `addAcceptTag()`、`addItem()`、`addRejectTag()`、`addSlotAllowedTag()`、`addSlotTag()`、`canAddItem()`、`canAddItemReason()`、`clear()`、`clearAcceptTags()`
- `clearChangeEvents()`、`clearItemDefinitions()`、`clearRejectTags()`、`clearSlot()`、`clearSlotAllowedTags()`、`countItem()`、`defineSlot()`、`destroy()`、`equipFromBag()`
- `findItem()`、`findItemByTag()`、`getAcceptRule()`、`getAcceptTag()`、`getAcceptTagCount()`、`getCapacityPolicy()`、`getChangeEventAction()`、`getChangeEventBagId()`、`getChangeEventCount()`
- `getChangeEventEquipSlot()`、`getChangeEventItemId()`、`getChangeEventOtherBagId()`、`getChangeEventOtherSlot()`、`getChangeEventQuantity()`、`getChangeEventSlot()`、`getExtra()`、`getId()`、`getItemCategory()`
- `getItemDefinitionCount()`、`getItemDisplayName()`、`getItemEquipSlot()`、`getItemExtra()`、`getItemMaxStack()`、`getItemVolume()`、`getItemWeight()`、`getKind()`、`getMaxVolume()`
- `getMaxWeight()`、`getName()`、`getRejectTag()`、`getRejectTagCount()`、`getSlotCount()`、`getSlotDurability()`、`getSlotInstanceId()`、`getSlotItemId()`、`getSlotName()`
- `getSlotProp()`、`getSlotQuantity()`、`getStackRule()`、`getUsedSlotCount()`、`getUsedVolume()`、`getUsedWeight()`、`hasAcceptRule()`、`hasCapacityPolicy()`、`hasItemDefinition()`
- `hasSlot()`、`hasStackRule()`、`isSlotEmpty()`、`itemHasTag()`、`moveSlot()`、`newBag()`、`newEquipmentSet()`、`registerItemsFromJson()`、`removeAt()`
- `removeItem()`、`restoreSnapshotJson()`、`setAcceptRule()`、`setCapacityPolicy()`、`setExtra()`、`setId()`、`setKind()`、`setMaxVolume()`、`setMaxWeight()`、`setSlotCount()`、`snapshotJson()`
- `setSlotDurability()`、`setSlotProp()`、`setStackRule()`、`slotHasTag()`、`splitStack()`、`swapSlots()`、`transferItem()`、`transferSlot()`、`unequipToBag()`

## 使用要点

- 将 `Bag` / `EquipmentSet` 保存在全局或实体状态中，不要每帧 `newBag`。
- 浮点重量/体积在脚本侧为 `float`。
- 自定义接纳/容量/堆叠回调只能在 C++ 注册；脚本通过策略名字符串选用。

**源码：** [`src/modules/inventory/`](../../../src/modules/inventory/)  
**设计文档：** [`docs/dev/背包系统设计.md`](../../dev/背包系统设计.md)  
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `inventory`。
