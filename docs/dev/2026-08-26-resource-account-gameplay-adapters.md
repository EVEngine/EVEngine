# ResourceAccount 玩法适配器收口说明

本次范围只覆盖 `ResourceAccount/Cost` 尚未接入的真实玩法路径，未修改
physics、procgen、editor、definition、ownership 活跃实现，也没有重写
`ActionRuntime` 的既有 action pipeline。

## 统一边界

`IResourceAccount` 是支付协议，不是所有资源的共同存储。每个适配器只把
一个已有领域所有者投影为该协议：

| 玩法 | 资源 | 权威所有者 | 适配器 |
| --- | --- | --- | --- |
| RPG / 卡牌 | Mana、Stamina | `AttributeSet` base value | `AttributeResourceAccountAdapter` 的 Mana/Stamina 命名视图 |
| Weapon | Ammo、Mana、Charges、Stamina | `Bag`、`AttributeSet` 或其他已绑定 account | `WeaponActionAdapter::resourceCost/fire` |
| 卡牌 | Mana | composition root 提供的玩家 account | `CardPlayPaymentAdapter` |
| 物品 | item quantity | `Bag::slots()` | `InventoryResourceAccount`、`ItemCostAdapter` |
| RTS | gold、wood 等账本资源 | `EconomyLedger` | `RTSEconomyAdapter` |

不建立第二个余额 map。Inventory 只调用现有 `InventorySystem` 的容量、堆叠、
事件路径；它自身只保存 reservation 状态。Attribute/Economy 适配器同样只
保存 reservation 状态，余额仍由原领域对象保存。

## 交易语义

`AtomicResourcePayment` 将领域 effect 放在资源 debit participant 之前：

1. effect `prepare`；
2. account `reserve`；
3. effect `commit`；
4. account `commit`；
5. 任一步失败都按 participant 合约执行 reverse-order `rollback`，已经
   提交的 effect 使用 `compensate`。

因此 Card 的状态切换和玩家 Mana 是一个交易；Weapon 的开火 effect 和 Ammo
扣除也是一个交易。`AccountNonce` 由被包装的实际 account 产生，视图不会重新
发号，也不会把一个账户的 reservation 交给另一个账户接受。

## Canonical 入口

`WeaponActionAdapter::fire` 是需要执行完整 Action 生命周期并原子扣除资源的唯一
同步入口；`resourceCost` 用于只读成本检查，`makeDefinition/makeRequest` 仅用于
需要由调用方持有 `ActionRuntime` 的异步/分阶段管线。不存在第二个 `submit` facade，
避免绕过资源支付而形成语义分叉。`CardPlayPaymentAdapter` 是
组合根使用的同步支付入口；它不负责查找“当前玩家”，避免隐式跨账户路由。

## 最小验证

`test/resource_gameplay_adapters.cpp` 覆盖：

- Mana/Stamina 命名视图复用同一 `AttributeSetResourceAccount`；
- Card 真实状态从 Hand 到 Played，并原子扣除玩家 Mana；余额不足时状态和
  Mana 都不变；
- Weapon 的 Ammo 真实存放在 Inventory Bag 中；effect prepare/commit 注入
  失败时 Ammo 和 effect 都不变；
- Item cost 使用 Bag 数量，并拒绝跨账户 reservation；
- RTS 通过 `EconomyLedgerResourceAccount` 完成真实 credit/payment；
- 已有 transaction participant 测试继续覆盖已提交 debit 的 compensation。

验证命令使用 `/tmp/evengine-resource-adapters` 独立目录，没有使用共享
`build/linux-debug`。

## 明确缺口

本次没有把所有玩法 action 的业务 effect 自动改造成 payment participant，
也没有把 Weapon 的弹匣字段、卡牌 condition、RTS production/build 流程强行
接入支付；这些属于跨写集整合，必须由对应领域 owner 提供可补偿的 effect
participant 后再单独接入。当前完成的是可复用的协议和 Card、Weapon/Inventory、
RTS 三条真实调用链，不宣称跨写集全面完成。
