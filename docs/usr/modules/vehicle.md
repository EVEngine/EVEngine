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

## 绑定与行为说明

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

## 2D 顶视轮胎驱动

连接 2D 物理世界的 `wheel` 移动现在通过轮胎接触力驱动刚体。
车速、朝向来自物理求解器；转向由前轮侧向力及其偏心力矩产生。
`ship`、履带保留各自移动路径；3D Box3D 射线悬架也接入同一轮胎提交/回执协议。

模型位于 XY 顶视平面：局部 +X 前进、+Y 侧向，安装点 `x/y` 和轮胎半径使用米，`z` 必须为 0。
它不把 XY 世界重力当成法向载荷，而用 `车身质量 * supportAcceleration2D / 轮数` 分配虚拟承重。
将该加速度设为 0 时轮胎离地，驱动仍能使轮子空转，但不向车身传递接触力。
真实墙体碰撞仍由原生求解器处理；虚拟地面不另建碰撞形状，不重复计算同一地面接触摩擦。

`suspension.wheels` 可指定安装点、`radius`、`drive`、`steer`。
未配置时使用前后两轮，位于车身半径的正负 0.7 倍，只有前轮转向；默认轮胎半径为
车身半径换算为米后的一半，限制在 0.05 到 0.5 米。下面参数位于 `suspension`：

| 参数 | 默认值 | 单位或含义 |
| --- | --- | --- |
| `wheelInertia` | 0.01 | 每轮正转动惯量，kg·m² |
| `tireFriction` | 1 | 联合滑移摩擦圆系数 |
| `longitudinalStiffness` / `corneringStiffness` | 10000 / 10000 | 纵向单位滑移 / 侧偏弧度对应的 N |
| `rollingResistance` | 0.015 | 滚阻系数 |
| `brakeTorque` | 1000 | 每轮最大制动力矩，Nm |
| `maxSteerAngle` | 0.6 | 转向轮最大偏角，rad，范围 [0, 1.4] |
| `supportAcceleration2D` | 9.81 | 顶视虚拟承重加速度，m/s² |

油门产生的总牵引力上限为 `min(driveForce, mass * accel / worldMeter)`，按驱动轮均分为力矩。
`maxSpeed` 保持像素/秒配置，在达到油门对应目标速度时停止继续加驱动力矩；不会强行截断碰撞或外力产生的车速。
`turnRate` 不再作为物理轮式车的强制角速度，转向轮使用 `maxSteerAngle`；`lateralGrip` 仍供旧 3D 路径使用。
偏心接触采用覆盖纵横耦合与多轮共享车身响应的保守对角上界，增加隐式阻尼以避免高刚度低速增能，
不是精确的多接触同时求解。`slipRatio/slipAngle` 是该隐式轮胎步的诊断值，下步会重新读取实际刚体点速度。

C++ 固定步顺序：

1. `VehiclePhysics::synchronizeWheelState(vehicle)` 发布上一已完成物理步的轮速和接触诊断。
2. `VehiclePhysics::submitWheelStep(vehicle, step)` 准备并提交当前步轮胎力，返回 `Result<VehiclePhysicsStatus>`。
3. 调用相同 tick 和 duration 的 `world.step(step)`，检查结果；失败时重试世界步，不重新施力。
4. 再调用 `synchronizeWheelState` 发布完成状态。

`Pending` 时完成状态不变；相同待处理步重复提交不会重复施力，不同待处理步会返回失败。
目标销毁或世界 restore 后，绑定失效，未发布候选被取消。仅 `Unavailable` 可以进入无物理的 kinematic 回退，
`Pending/Rejected/Cancelled` 不得触发另一套移动。
旧 `Vehicle.update(dt)` 路径自动执行发布与提交，仍需由调用方按相同 dt 推进物理世界。
调用和状态访问都在仿真 owner 线程完成，不支持 solver 回调内重入。

`SuspensionState::wheels` 是已完成轮速的唯一权威；binding 只持有待完成候选和只读回执。
轮胎运行态尚未纳入 VehicleDefinitionRuntime 的存档格式，完整轮胎恢复仍属于后续验收项。


### 3D 射线悬架的后端与数值边界

Box3D 的轮式/悬架车辆使用射线接触、弹簧阻尼法向力和联合滑移轮胎力。
局部 +Z 前进、+X 侧向、+Y 向上，轮安装点和半径使用米。
接触点相对速度包含车身和地面的角速度，以及材质传送带速度；动态地面承受等大反向力。

一次提交固定接触几何，并用 64 次有界 Jacobi 迭代预测多个车轮对共享刚体的作用。
每次迭代从原轮速计算候选，驱动/制动力矩每个物理步只积分一次。
这是固定步游戏模型，并非接触拓扑随迭代更新的精确求解器；仍需按车辆尺度选择子步。
坡道驻车、动态地面、移动地面自转以及提交失败的回执行为均有独立回归测试。

当前 3D 轮胎适配需要 Box3D；Jolt 返回结构化 Unsupported，不进入运动学回退。
这不限制 Jolt 使用通用 ForceBatch、力场或翼面气动力。
