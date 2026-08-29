# Container / Zone / Transfer 协议

本文记录整改清单第 25 项的实现约束。总 checklist 由主代理维护；本文件只描述已经落地的公共协议和 adapter 合同。

## 边界与所有权

`src/engine/common/Container.h` 只定义 `ContainerId`、`MembershipId`、`ContainerObject`、`ContainerSnapshot`、`IContainer`、`Zone<Space>` 和 `TransferService`。它不包含 Card、Inventory、Vehicle、Building、ECS、UI 或渲染类型。

每个 adapter 是借用视图，不拥有被适配的领域对象：

| adapter | 唯一权威 membership 状态 | stale 检测 | restore / hot-reload 重建 |
| --- | --- | --- | --- |
| `CardContainerAdapter` | Deck / Hand / discard 的现有 Card 指针集合 | Card snapshot 使用 generation-qualified `ecs::EntityHandle` | 先恢复 Card ECS entity，再由当前 Deck/Hand/discard 集合重新创建 adapter |
| `InventoryContainerAdapter` | Bag / EquipmentSet 的 slot 数据 | adapter revision 与对象实例 ID | 先恢复 Bag / EquipmentSet，再绑定新 adapter |
| `VehicleSeatContainerAdapter` | `VehicleEntity::Seats` component | vehicle handle generation + Seats revision | 先恢复 Vehicle entity/component，再绑定新 adapter |
| `BuildingGarrisonContainerAdapter` | `PlacedBuilding::garrison` 与 `garrisonRevision` | building instance 是否仍在 `PlacementWorld` + revision | 先恢复 PlacedBuilding，再绑定新 adapter |

adapter 不删除对象；领域 owner 必须先清理所有借用列表，再销毁对象。`PreparedState` 返回后，adapter 与其领域 owner 必须保持存活到 `commit()` 或 `rollback()` 完成；违反该生命周期合同属于不可恢复的程序错误。正常 stale 变化在 `snapshot()` / `prepare()` 阶段以 `Result` 返回。

`ContainerSnapshot` 是进程内事务值，不是持久化格式。若要跨进程或落盘，必须由外层 SnapshotEnvelope 包装 schema id、schema version 和 unknown-field policy；恢复时通过 adapter 的候选状态与 `prepare()` 原子发布，不能先写入半恢复 membership。

## Zone

`Zone<Space>` 的 `Space` 只能是 `ScreenSpace`、`World2DSpace`、`World3DSpace` 或 `GridSpace`。`Coordinate<ScreenSpace>`、`Coordinate<World2DSpace>`、`Coordinate<World3DSpace>`、`Coordinate<GridSpace>` 没有隐式转换；3D 坐标始终带 `z`。形状类型也按空间约束：2D 使用 `Rectangle<Space>` / `Circle<Space>`，3D 使用 `Box3D` / `Sphere3D`。

Zone 明确拥有以下元数据：

- `ContainerId`：跨模块定位所需的稳定身份；
- shape：只在声明的 coordinate space 中解释；
- `AcceptedCondition`：无副作用的 `Filter`，在 entry 前评估；
- `Capacity`：membership 数量上限；
- `Ordering`：`Insertion` 或 `ExplicitSlots` 等布局语义。

因此 Screen、World2D、World3D、Grid 不能通过公共接口混用；转换必须由拥有坐标变换规则的领域模块显式完成。

## Transfer 原子性

`TransferService::transfer()` 的顺序固定为：读取双方 snapshot → 校验 revision / membership / slot / accepted condition → 构造双方 candidate → 对所有参与者调用 `prepare()` → 全部成功后 commit。任何 preflight 或 prepare 失败都不触碰 membership；这也是 failure-injection 测试的要求。

`PreparedState::commit()` / `rollback()` 不抛异常，并且 commit 所需分配必须在 prepare 阶段完成。事件观察者在 commit 后调用；观察者异常只能形成 warning，不得把已经提交的 transfer 报告为失败。并发访问由调用方在 adapter 外部串行化。

## 统一生命周期事件

Vehicle Seat 与 Building Garrison 的 `enter()` / `exit()` 使用 `eve::game_event::EventEnvelope`，共享：

- schema id：`container:event`；schema version：`1`；
- type：`container.accepted`、`container.rejected`、`container.enter`、`container.exit`；
- source：ContainerId；subject：MembershipId；
- tick：调用方注入的 `SimulationTick`；
- payload：包含 containerId、objectId、slot、revision，拒绝时附 reason；
- eventId：由 container、object、kind 和 adapter-local attempt serial 确定性派生。

accepted / enter 只在 commit 完成后产生；rejected 只表示本次尝试没有改变 membership。Card draw、shuffle、layout 仍属于 Card 领域，不下沉到 common Container 协议。

## 验证入口

`test/container_transfer.cpp` 保留 common、Card、Inventory、Zone 与 staged transfer 的既有覆盖；`test/container_adapters.cpp` 覆盖：

- Vehicle Seat 与 Building Garrison 的 descriptor / ordering / capacity parity；
- Screen / World2D / World3D / Grid 的编译期不可混用；
- canonical EventEnvelope 的 schema、type、source、tick 与 commit 后顺序；
- rejected、callback failure、stale prepare、跨 adapter transfer；
- capacity failure 后 revision 与 membership 不变。
