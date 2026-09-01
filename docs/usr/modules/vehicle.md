# 载具系统模块

**脚本入口：** `eve.Vehicle()`（武器挂点还需要 `eve.Weapon()`）

Vehicle 提供数据驱动的载具定义、移动命令、座位、玩家控制、装甲伤害以及可选的
Box2D/Box3D 连接。载具是 `VehicleEntity` ECS 实体，可以通过
`eve.view(eve.VehicleEntity)` 观察；模块负责创建、命令和逐帧更新。

完整的 2D 驾驶、RTS 移动和炮塔示例见 `examples/vehicle`。

## RTS 移动与攻击

```squirrel
local vehicle = eve.Vehicle();
vehicle.registerVehiclesFromJson(@"[
  {""id"":""scout"", ""mobility"":""kinematic"", ""maxSpeed"":20,
   ""accel"":8, ""turnRate"":120, ""maxHealth"":100}
]");
local scout = vehicle.newVehicle("scout", 0.0, 0.0, 0.0, "blue");
vehicle.moveTo(scout, 30.0, 10.0);

// 每个固定模拟步调用：
vehicle.update(dt);
if (vehicle.isArrived(scout)) vehicle.hold(scout);
```

`moveTo`、`attackMove`、`attack`、`hold` 和 `stop` 写入/替换命令队列；用
`orderCount` 和 `getCurrentOrderType` 显示状态。Weapon 和 Vehicle 都需要更新时，
每个模拟步各更新一次。

## 座位、玩家输入和物理

```squirrel
local car = vehicle.newVehicle("car", 100.0, 80.0, 0.0, "player");
if (!vehicle.attachPhysics2D(car, physicsWorld))
    print("attach physics failed\n");
vehicle.enterSeat(car, 0, 1001);
vehicle.setPlayerControls(1001, throttle, steer, brake, fire, aimYaw, aimPitch);
```

一个 player ID 同时只能占用受实现约束的座位；离开时用 `exitSeat` 或
`exitSeatByPlayer`。物理 World 必须比载具活得久；销毁 World 或切换空间前先调用
`detachPhysics`。不要跨帧保存 `getMount` / `getSeatMount` 返回的借用引用。

## API 快查

### 定义、创建与移动

| API | 说明 |
|---|---|
| `registerVehiclesFromJson(json)` / `clearVehicleDefinitions()` | 注册或清除载具定义。 |
| `getVehicleDefinitionCount()` / `hasVehicleDefinition(id)` | 查询 definition catalogue。 |
| `getVehicleDefinitionMobility(id)` / `getVehicleDefinitionMaxHealth(id)` | 查询机动模型和最大生命值。 |
| `getMobilityCount()` | 当前可用原生 mobility 实现数量。 |
| `newVehicle(defId,x,y,heading,faction)` | 创建载具；未知 definition 返回 null。 |
| `moveTo(v,x,y)` / `attackMove(v,x,y)` / `attack(v,x,y,targetId)` | 下达移动或攻击命令。 |
| `stop(v)` / `hold(v)` / `clearOrders(v)` | 停车、原地待命或清空命令。 |
| `orderCount(v)` / `getCurrentOrderType()` / `isArrived(v)` | 查询命令和到达状态。 |
| `update(dt)` | 推进命令、机动、座位控制和事件。 |

### 位置、阵营和生命值

| API | 说明 |
|---|---|
| `getId()` / `getDefId()` | 实体和 definition ID。 |
| `getX()` / `getY()` / `getHeading()` / `getSpeed()` / `getHeight()` | 世界位置、朝向、速度和高度。朝向单位为度。 |
| `setPosition(v,x,y)` / `setHeading(v,degrees)` | 在无物理冲突的安全点设置 transform。 |
| `getFaction()` / `setFaction()` | 查询或修改阵营标签。 |
| `getHealth()` / `getMaxHealth()` / `setHealth()` | 当前和最大生命值。 |
| `applyDamage(v,amount,zone,sourceId)` | 对指定装甲区施加伤害。 |
| `getArmorZoneMult(v,zone)` / `isDestroyed(v)` | 查询装甲倍率或销毁状态。 |

### 座位、挂点与控制

| API | 说明 |
|---|---|
| `getSeatCount()` / `getSeatName()` / `getSeatCameraMode()` | 枚举定义中的座位。 |
| `isSeatOccupied()` / `getSeatOccupant()` / `getSeatMount()` | 查询占用者和座位武器挂点。 |
| `enterSeat()` / `exitSeat()` / `exitSeatByPlayer()` | 管理玩家与座位关系。 |
| `setPlayerControls(playerId,throttle,steer,brake,fire,yaw,pitch)` | 写入指定玩家本模拟步的控制。 |
| `setInput(v,throttle,steer,brake)` | 直接写入载具控制输入。 |
| `getMountCount()` / `getMount(v,i)` | 枚举载具定义创建的 WeaponMountEntity。 |

### 物理与事件

| API | 说明 |
|---|---|
| `attachPhysics2D(v,world)` / `attachPhysics3D(v,world)` | 将载具连接到对应物理空间；返回是否成功。 |
| `detachPhysics(v)` / `hasPhysics(v)` / `getPhysicsSpace(v)` | 断开或查询物理连接。 |
| `getEventCount()` / `getEventType(i)` / `clearEvents()` | 枚举并清空载具事件。 |
| `getEventVehicleId()` / `getEventDefId()` | 事件关联载具和 definition。 |
| `getEventOrderType()` / `getEventX()` / `getEventY()` | 事件的命令类型和目标位置。 |

## 生命周期、阶段和所有权

- Vehicle 拥有载具 ECS 状态，但不拥有 Physics World、玩家或 Weapon 模块。
- 物理连接是显式跨域 Link：两个销毁顺序都应先 `detachPhysics`，热重载后重新建立。
- 推荐固定步长调用 `update(dt)`；不同 dt 序列可能产生不同的到达时刻和转向轨迹。
- 消费完事件再 `clearEvents()`；不要在 `eve.view` 遍历过程中创建载具或改变 ECS 结构。
