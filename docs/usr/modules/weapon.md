# 武器系统模块

**脚本入口：** `eve.Weapon()`

Weapon 提供数据驱动的武器定义、弹匣/储备弹药、射击模式、扩散与后坐力、装填、
载具挂点和角色持握 Rig。武器、挂点、Rig 与弹药池都是 ECS 实体；脚本可用
`eve.view(eve.WeaponEntity)` 查询，但创建和状态变更应通过模块 API 完成。

## 注册并使用武器

```squirrel
local weapon = eve.Weapon();
weapon.registerWeaponsFromJson(@"[
  {""id"":""rifle"", ""logic"":""hitscan"", ""damage"":18,
   ""range"":120, ""cooldown"":0.12,
   ""ammo"":{""mag"":30,""reserve"":90,""reload"":1.8}}
]");
local rifle = weapon.newWeapon("rifle");
weapon.setAim(rifle, 15.0, -2.0);
if (weapon.canFire(rifle)) weapon.fireAt(rifle, 10.0, 0.0, 30.0, 1001);
weapon.update(dt);
```

注册函数返回的状态必须检查；未知 definition 会令 `newWeapon` 返回 null。
`fireAt`/`attack` 只生成武器域的射击结果和事件，实际命中、生命值扣除及表现由消费
系统负责。每帧以模拟 `dt` 调用一次 `update(dt)`，以推进冷却和装填。

## 弹药池与挂点

```squirrel
local pool = weapon.newAmmoPool("rifle", 180);
weapon.bindAmmoPool(rifle, pool);
local turret = weapon.newMount("turret");
weapon.mountSetLimits(turret, -120.0, 120.0, -10.0, 45.0, 90.0, 60.0);
weapon.mountAttachWeapon(turret, rifle);
weapon.mountAimAt(turret, 35.0, 5.0);
```

共享弹药池的数量是权威值，解绑不会复制弹药。`mountDestroy` 后挂点引用只可用于
判断销毁状态，不应继续瞄准或换装。

## 绑定与行为说明

### 定义、创建与更新

| API | 说明 |
|---|---|
| `registerWeaponsFromJson(json)` / `clearWeaponDefinitions()` | 注册或清除武器定义；不要在仍有实例时热清除定义。 |
| `getWeaponDefinitionCount()` / `hasWeaponDefinition(id)` | 查询 definition catalogue。 |
| `getWeaponDefinitionLogic(id)` / `getWeaponDefinitionDamage(id)` / `getWeaponDefinitionRange(id)` | 查询定义的逻辑、基础伤害和射程。 |
| `getLogicCount()` | 当前注册的原生 WeaponLogic 数量。 |
| `newWeapon(defId)` / `newMount(type)` / `newRig(wield)` | 创建 ECS 武器、挂点或持握 Rig。 |
| `update(dt)` | 推进所有武器的冷却、装填和状态阶段。 |

### 武器、瞄准和射击

| API | 说明 |
|---|---|
| `getId()` / `getDefId()` / `getOwnerId()` / `setOwnerId()` | 武器实体身份和逻辑 owner。 |
| `getMagAmmo()` / `getReserveAmmo()` / `getResourceValue()` | 当前弹匣、储备或通用资源值。 |
| `getSpread()` / `getRecoilPitch()` / `getRecoilYaw()` | 当前扩散与累计后坐力。 |
| `getFireMode()` / `setFireMode()` | 查询或切换射击模式。 |
| `getSelectableModeCount()` / `getSelectableMode(i)` | 枚举定义允许玩家选择的模式。 |
| `isAiming()` / `setAiming()` / `getZoomFov()` | 瞄准状态与建议 FOV。 |
| `setAim(yaw,pitch)` / `getYaw()` / `getPitch()` | 设置或查询武器瞄准角（度）。 |
| `canFire()` / `fireAt(x,y,z,shooterId)` / `attack(yaw,pitch,shooterId)` | 检查并发起射击。 |
| `startReload()` / `cancelReload()` / `isReloading()` | 控制和查询装填。 |
| `getCooldown()` / `getStage()` | 查询冷却秒数和当前逻辑阶段。 |

上述同名方法既可能出现在 `Weapon` 模块，也可能出现在 `WeaponEntity` 上；优先使用
实体方法做单对象操作，批处理和定义管理使用模块方法。

### 弹药、挂点和 Rig

| API | 说明 |
|---|---|
| `newAmmoPool(ammoType,count)` / `getAmmoType()` / `getCount()` / `setCount()` | 创建和操作共享弹药池。 |
| `addAmmo(n)` / `ammoPoolAdd(pool,n)` / `ammoPoolGetCount(pool)` | 增加或读取弹药。数量不会低于零。 |
| `bindAmmoPool()` / `unbindAmmoPool()` / `getAmmoPool()` | 连接、断开或查询武器的共享弹药池。 |
| `mountAttachWeapon()` / `mountGetWeapon()` / `mountSetLimits()` | 挂载武器并设置 yaw/pitch/转速限制。 |
| `mountAimAt()` / `aimAt()` / `mountDestroy()` / `destroy()` / `isDestroyed()` | 瞄准或销毁挂点。 |
| `getType()` / `getNodePath()` / `setNodePath()` / `getWeapon()` | 查询挂点/Rig 的类型、场景节点和武器。 |
| `newRig()` / `rigAttachWeapon()` / `rigGetWeapon()` / `rigSetPose()` | 创建并操作持握 Rig。 |
| `getWield()` / `attachWeapon()` / `setPose()` | Rig 实例的持握类型、武器和局部 pose。 |

### 事件

| API | 说明 |
|---|---|
| `getEventCount()` / `getEventType(i)` / `clearEvents()` | 枚举并清空本帧武器事件。 |
| `getEventWeaponId()` / `getEventDefId()` / `getEventMountId()` | 事件关联身份。 |
| `getEventAmmoLeft()` / `getEventArc()` / `getEventAoe()` | 事件资源和范围参数。 |
| `getEventSpread()` / `getEventPellets()` | 本次射击的扩散与弹丸数。 |
| `getEventRecoilPitch()` / `getEventRecoilYaw()` | 本次射击后坐力。 |
| `getEventDamageType()` / `getEventElement()` | 伤害分类和元素标签。 |

事件索引只在下一次结构修改或 `clearEvents()` 前有效。建议 update 后统一消费，再清空。

## 战斗载体与技能片段

`CombatCarrierRuntime` 是脚本拥有的确定性投射物池：配方由弹道算子、触发器与
效果组合而成。可用预设 table/JSON 注册，也可用 Noita 风格片段数组一次
`compileFragments` / `cast`（修正片段在前，投射物片段在后）。

```squirrel
local weapon = eve.Weapon();
local created = weapon.newCarrierRuntime(64);
// 或 weapon.newCarrierRuntimeDefault() —— 默认容量 256
local pool = created.value;   // ownership = "owned"
pool.ownership();             // "owned"
pool.configurePool(64);

pool.registerRecipe({
  id = "spell:shell", lifetime = 2.0, speed = 12.0,
  damage = 10, element = "fire", homing = 90, pierce = 1
});
// 等价：pool.registerRecipeJson(jsonString)

// Homing / proximity 必须用 castAtTarget 提供正目标 id；普通投射物可用 cast。
pool.castAtTarget([
  { kind = "fan", count = 5, spread = 40 },
  { kind = "homing", turnRate = 120 },
  { kind = "projectile", id = "spell:bolt", speed = 14, damage = 8, lifetime = 2, element = "arcane" }
], px, py, pz, dx, dy, dz, enemyId, enemyGen);
// cast(...) / castJson(json, ...) —— 无制导目标时使用
// compileFragments(fragments) / compileFragmentsJson(json) 只编译并注册，不生成

pool.addHitTarget(enemyId, enemyGen, ex, ey, ez, 0.5);
pool.setTargetPosition(enemyId, enemyGen, ex, ey, ez);
local frame = pool.update(dt);   // Result：events / eventCount / activeCount
pool.clearHitTargets();
pool.clearTargets();
```

| API | 说明 |
|---|---|
| `newCarrierRuntime(capacity)` / `newCarrierRuntimeDefault()` | 创建脚本拥有的 `CombatCarrierRuntime`（Result.value）。 |
| `ownership()` / `capacity()` / `activeCount()` | 查询所有权标记、池容量与存活载体数。 |
| `configurePool(capacity)` | 空池时调整容量（1..1048576）。 |
| `registerRecipe(table)` / `registerRecipeJson(json)` | 注册预设或完整配方（脚本路径拒绝 `avoidBody`）。 |
| `compileFragments(fragments)` / `compileFragmentsJson(json)` | 编译片段栈并注册；返回 recipeId / volley 信息。 |
| `cast(...)` / `castAtTarget(...)` / `castJson(...)` | 编译 + 注册 + 生成；含 homing/proximity 时必须 `castAtTarget`。每次 cast 会分配唯一 `recipeId`，避免覆盖飞行中的载体配方。 |
| `spawn(recipeId,x,y,z,dx,dy,dz)` / `spawnAtTarget(...)` / `spawnVolley(...)` | 按已注册配方生成一枚或多枚载体。 |
| `update(seconds)` | 推进整池；始终使用脚本 hit/target 探针（可为空）。 |
| `addHitTarget(...)` / `clearHitTargets()` | 注册或清空圆形命中目标。 |
| `setTargetPosition(...)` / `clearTargets()` | 注册或清空制导目标位置。 |

载体事件（`frame.value.events`）携带 `trigger` / `impact` / `damage` / `element` /
位置 / `nx,ny,nz` / `sourceId` / `targetId`；Weapon 不结算生命值——把事件交给
combat / settlement 等权威 owner。片段 `kind` 与配方字段见
`docs/dev/战斗载体与投射物行为组合框架.md`。

## 生命周期与确定性

- 定义先注册、实例后创建；热重载定义时应在安全点重建依赖的实例。
- 所有角度使用度，时间参数使用秒；回放需注入相同 `dt` 和相同射击输入序列。
- ECS view 中不要创建/销毁武器或挂点，先收集 ID，遍历结束后再做结构修改。
- Weapon 不拥有伤害目标；用事件把射击结果交给 combat/vehicle 等权威 owner。

## 高级弹道算子与触发器

`CombatCarrierRuntime` 在原有组合模型上增加以下完整配方 `motion` 算子；
`ProjectileRuntime` 的三模式轻量接口继续可用。算子按序执行，**唯一的 `linear`
必须放在最后**。`sway` / `helix` 位于其余运动算子之后、`linear` 之前，多个曲线可以叠加。

- `rotate`：`axisX/axisY/axisZ`（默认 0/1/0）与带符号 `angularRateDegrees`，按右手规则绕固定轴转动速度。
- `orbit`：同一轴、带符号 `angularRateDegrees`、`orbitRadius`、`turnRate`。根据目标平面内切向和径向偏差限速转向；不是强行锁定位置的圆轨道。目标可移动，必须 `spawnAtTarget` 并提供位置。
- `predictive`：`turnRate`、`predictionSeconds`。首帧直接追踪，随后用连续目标位置差估计目标速度，以当前距离/速率估算飞行时间并限制预瞄时长。它是运动外推，不承诺命中加速目标；不读取 ECS 对象地址。
- `drag`：`drag` 为非负指数衰减系数，速率乘 `exp(-drag*dt)`，不会因大步长反向。
- `speedOscillate`：`amplitude` 为速度幅度、`frequency` 为 Hz。速率为 `max(0, recipe.speed + amplitude*sin(2*pi*frequency*age))`，停止后保留朝向，可以再次启动。它覆盖前面算子的速率，因此将它放在 `accelerate` 后会覆盖加速度结果。
- `wander`：`turnRate`、`amplitude`（无量纲方向扰动）、`frequency`（平滑噪声采样 Hz）、非负 `randomSeed`。种子与 slot/generation 混合；按绝对模拟年龄哈希采样并平滑插值，不使用全局随机源。同一池状态和输入可精确重放，不同投射物获得不同运动。
- `accelerate` 现接受带符号 `acceleration`；减速到零不反向。正向加速可沿最后有效朝向重新启动。
- `sway` / `helix` 的幅度现在是有界位移幅度，不会把横向速度每帧累加成持续增速。每步使用正弦/余弦位移差，快照保留上一段曲线速度贡献。

新增 `triggers`：`{kind="onDistance", threshold=20.0}`（累计积分路程首次跨越阈值）、
`{kind="onSpeedBelow", threshold=2.0}`（速度由阈值上方降至阈值或下方）、
`"onApex"`（y 速度从正值跨越到非正）。它们可以绑定现有 `emitHit`、`splash`、
`spawnChild`、`release` 等效果。距离不是离出生点的直线距离。
`onProximity` 保留每步处于范围内都会触发的语义；不是仅首次进入触发。

```squirrel
local created = weapon.newCarrierRuntime(128);
if (!created.ok) throw created.status.summary;
local pool = created.value;
local registered = pool.registerRecipe({
    id = "spell:wandering-missile", lifetime = 4.0, speed = 12.0,
    motion = [
        {kind="predictive", turnRate=120.0, predictionSeconds=0.5},
        {kind="accelerate", acceleration=2.0},
        {kind="wander", turnRate=25.0, amplitude=0.3, frequency=2.0, randomSeed=73},
        "linear"
    ],
    triggers = ["onExpire", {kind="onDistance", threshold=25.0}],
    impacts = [{kind="release", on="onExpire"}, {kind="emitHit", on="onDistance", damage=12.0}]
});
if (!registered.ok) throw registered.status.summary;
```

新增 `pool.states()` 返回 Result，`value` 是稳定 slot 顺序的拥有型数组；每项含
`handle`、`recipeId`、`x/y/z`、`vx/vy/vz`、`age`、`distanceTravelled`。
返回后与池无借用关系，可供渲染消费；复制成本与存活数量线性增长，通常每渲染帧取一次。
新算子通过完整配方注册；原有 `cast` 片段语法继续支持原先的片段，不把新参数隐式解释为片段。

### 更新、反弹与回放契约

每步只积分到剩余寿命，不因一个过大的 dt 让已过期载体继续飞行。
定时事件按模拟时间排序，同一时间的到期释放最后处理；释放后不再执行较晚触发器。
每载体每次外层更新默认最多处理 4096 次触发，预算可配置；超出返回诊断并回滚整帧，应减小 dt 或显式增大预算。
配方最多 64 个运动算子、64 个触发器、256 个效果。触发 kind 不得重复，效果必须有
匹配触发器。定时和路程事件的位置在本步扫掠线段上按时间插值，接触事件使用真实
接触点；曲线在单步内仍以线段近似，因此小步长对空间精度同样重要。

反弹按扫掠段的命中时间比例排序，到接触点后反射速度并继续消耗剩余时间；同一更新可以多次反弹。
命中瞬时速度在线段起终点速度间按时间插值，反弹同时应用摩擦、恢复系数和确定性散射。
反弹造成的速度阈值/顶点跨越在命中时刻触发；定时事件和里程事件不会因较早反弹而丢失。
脚本球体探针提供首次表面交点及时间比例，忽略从表面离开的零时刻接触。
穿透后按完整目标身份（原生 table/type/id/generation；脚本 id/generation）去重，本载体不再命中这个目标；换代后的实体可以重新命中。
历史进入快照，最多 65536 项；超限失败。无有效目标 id 的 C++ 几何接触不参与目标去重。
C++ 探针须提供有限坐标、有限且非零法线；非法输入或反射溢出回滚整帧。

池是唯一状态 owner，调用限 owner thread；provider 仅在同步 update 内借用，
不得从回调中重入修改同一池，返回后可销毁。缺少目标/命中/规避 provider 是明确失败，
不会静默忽略算子。活跃配方可以重复注册相同内容，但修改内容须先释放使用它的载体，
防止飞行中参数与恢复语义悄悄变化。

`CarrierRuntimeSnapshot` 为内存中的强类型快照，schemaVersion=4，包含路程、最后朝向、
曲线贡献、目标采样位置与年龄、穿透目标历史、求解设置与配方上下文。恢复前验证所有槽、generation、有限值、年龄、预算和
所需目标，以及已注册配方是否与快照一致，失败不发布任何状态。
不支持其它版本；旧快照必须由外部适配器重建新状态，不能仅改版本号。
此类型不是跨进程 wire/save 编解码协议；持久化方须显式编解码并拒绝未知版本。
同一编译器/后端、相同步长、种子、发射顺序和 provider 样本可重放；跨后端采用 1e-6
模拟单位容差，变步长与固定步长不承诺相同轨迹。

### module-interface: weapon（此次扩展）

E1/E2：weapon 在冷路径绑定时查询可选 `IWorldSphereSweepFactory`，physics 提供；不增加跨模块头或硬链接依赖。
E3：`CombatCarrierRuntime` 独占 slot/generation 状态；目标只保留 generation handle 和位置副本，
目标先销毁由 provider 返回失败并使帧回滚，池先销毁不销毁目标；恢复后由调用者重新提供服务。
E4：`CarrierFrame` 在整帧提交后发布拥有型事件，按 carrier slot 和每载体触发时间排序。
E5：`CombatCarrierRuntime.states()` 增加零实参快照读取，新运动和触发参数复用唯一
`CarrierRecipeCodec`，table / JSON 不维护第二套规则。E6：不增加工具协议。
不新增 ECS System，不增加持久化 wire 格式，不声明新的模块裁剪等级。

运行演示：[advanced-ballistics](../../../examples/advanced-ballistics/README.md)。
组合与失败测试：`test/integration/projectile-behavior/advanced_carrier.cpp`。

反弹还可配置 `bounceFriction`（0..1 的切向速度损耗）、`bounceSpreadDegrees`
（0..90 度的散射锥半角）和 `randomSeed`。默认值均为零，保留镜面反射；
摩擦为 1 时完全去掉切向速度，恢复系数继续缩放剩余速度。散射使用独立哈希随机流，
快照回放保持一致，输出方向限制在接触面外侧半球。若希望真实能量不增长，将
`restitution` 限制在 0..1；大于 1 表示明确的增能弹跳玩法。


### 自己写函数计算弹道与速度

`pool.updateWithMotion(dt, function(state, proposed, step) { ... })` 对每个存活投射物的每个提议运动段调用函数；子步和反弹剩余段都会再次调用。
`state` 是步开始的 owning 副本：`x/y/z`、`vx/vy/vz`、`age`、`lifetime`、
`distanceTravelled`、`recipeId`、`slot`、`generation`；`proposed` 是本步内置运动算子计算出的
`x/y/z/vx/vy/vz`；`step` 是裁剪到剩余时间、寿命和配置上限的提议段秒数；碰撞可能只消费其中一部分。按稳定槽位顺序执行，零 dt 不调用。

```squirrel
function myVelocity(state, proposed, dt) {
    local speed = 100.0 + 40.0 * sin((state.age + dt) * 3.0);
    return {vx=speed, vy=30.0*cos(state.age*2.0), vz=0.0};
}
local result = pool.updateWithMotion(dt, myVelocity);
if (!result.ok) throw result.status.summary;
local events = result.value.events;
```

返回方式：

- `null`：完全保留内置算子结果。
- `{vx, vy, vz}`：替换速度，由引擎从旧位置积分一次。
- `{x, y, z}`：直接计算本步终点，引擎用位移 / step 推导速度。
- `{x, y, z, vx, vy, vz}`：同时指定终点与瞬时速度，适合参数曲线的解析导数。

必须提供完整的三维坐标组；未知字段、缺失分量、非数值、NaN/Infinity 都失败。
轨迹函数可通过闭包捕获只读参数，通过 `recipeId`/handle 分派不同算法。
例如从 `(-5,-2,0)` 发射的椭圆轨道：

```squirrel
function myPath(state, proposed, dt) {
    local t = state.age + dt;
    return {x=-5.0+5.0*sin(t*1.4), y=-2.0+3.0*(1.0-cos(t*1.4)), z=0.0};
}
```

执行顺序为 **内置运动 → 自定义函数 → 扫掠碰撞 → 触发器/反弹/回收**。
自定义轨道的碰撞和里程使用本步起终点之间的线段；弯曲很强时应缩小固定步长。
绝对位置函数会在下一步重新指定轨道；若要保留反弹方向，应根据 `state.vx/vy/vz` 继续计算，
或返回 `null` 交还内置运动。返回值完全等于 `proposed` 时保留内置曲线的增量记账。
子弹生成延后到帧末，新生成的子弹从下一步开始调用函数。

函数在 VM 所属线程同步执行，不保存回调，也不持有锁。重入更新、发射、修改目标或配方被拒绝。
函数抛异常、返回无效数据或后续命中 provider 失败时，整帧投射物状态与事件不提交。
**函数必须无外部副作用**：引擎无法撤销闭包对全局变量的修改、日志或外部随机数消费。
回放/恢复时重新传入相同函数和参数；函数代码与闭包不进入 snapshot v4。
热重载后下一步传入新函数即可，改变函数实现意味着改变回放上下文。

C++ 实现 `ICarrierMotionFunction::evaluate(before, proposed, step)`，返回
`Result<CarrierMotion>`，通过 `runtime.updateWithMotion(dt, function, targets, hits, bodies)` 使用。
位置和速度均需返回；失败或异常中止整帧。原有 `update()` 不调用用户函数。
此接口每弹每提议段一次虚调用；脚本桥额外构造 owning 参数表，较内置算子更昂贵，宜按固定模拟步使用。

module-interface 补充：运动函数是 weapon 拥有契约的同步输入策略，单个已知调用者、每弹每提议段调用、
与本帧原子提交绑定（R-MECH-1..4）；无 capability 注册或字符串运行时查找，无新增 ECS/Link、
跨域事件或工具协议。脚本新增 `updateWithMotion`；输出仍是现有 CarrierFrame。
provider 缺席走原 `update()`；函数失效不会跨帧保留；没有新增持久化格式。


### 连续反弹与有界子步

```squirrel
// 在空池上配置：最大段长、每弹迭代数、接触数、触发数（均为每次外层更新预算）。
local configured = pool.configureSimulation(1.0/120.0, 256, 64, 4096);
if (!configured.ok) throw configured.status.summary;
```

默认 `maxStep=0` 沿用调用方传入的固定步长；大于零时自动细分。预算范围 1..65536。
更新会消费所有未过期的时间，或整帧失败并回滚；不会把余时截掉。零时刻反复碰撞也受预算约束。
预算耗尽返回 `Conflict`；调用者可用较小 dt 重试，不应在失败后直接丢弃该段时间。
配置仅允许空池修改，快照捕获/恢复配置，避免飞行中隐式改变回放上下文。

`ICarrierHitProbe::Contact.fraction` 是沿提议段的规范命中时间比例 [0,1]；`point` 是该时刻
载体中心，`normal` 指向障碍外侧。返回接触列表会按时间排序。旧探针缺省 fraction 的接口仅为
兼容：在边界将 contact.point 投影到扫掠线段，转换为同一规范比例；新探针应显式提供 fraction。
当前接口仍是线段扫掠，不是曲线精确求交，也没有自动接入物理世界形状扫掠。

`CarrierEvent.age` / 脚本事件 `age` 为载体发生事件时的年龄（秒），按槽位和该载体事件时间有序；
同一时刻到期释放最后处理。`onProximity` 在每个实际处理的段终点做范围判定，细分会增加触发频率。
子弹生成仍延后到整个外层更新末尾；新子弹本帧不追赶父弹剩余时间。

自定义函数必须是纯函数：每次输入是该段开始的已暂存状态，碰撞后下次输入已带反射速度和接触位置。
对任意自定义函数，命中速度采用段首/段尾速度的线性近似；减小 maxStep 提高曲线与加速度精度，
不承诺跨步长、跨后端位级相同。外层目标 provider 每弹采样一次；预测速度用上次采样年龄差计算，
本次各子步复用该样本，自动细分不会伪造更大的目标速度。

模块契约补充：更新仍为 owner-thread 同步事务，不新增 ECS System、能力注册、向上依赖或工具协议。
碰撞 provider 输入输出仍为 owning 值/即时借用；snapshot v4 是本地强类型状态，不新增 wire 格式。
v2 不可仅改版本号恢复：外部迁移需明确选择求解设置、重建目标采样上下文和穿透历史；没有迁移证据时拒绝恢复。

### 可选碰撞与物理世界扫掠

配方（完整形式和 preset 均支持）可配置 `collision = { enabled = true, worldSweep = false }`。
默认值保持已有行为：只有声明 `onHit` 的投射物才查询普通碰撞探针。
`enabled=false` 完全跳过碰撞检测及 `onHit` 动作，轨道、速度函数和其他触发器继续运行；
即使 `worldSweep=true`，关闭碰撞时也不要求物理世界存在。
`enabled=true, worldSweep=true` 选择独立注入的物理世界扫掠探针，替代普通探针，不叠加查询。
没有 `onHit` 消费者时不执行无用查询。普通探针也可以做连续几何检测；`worldSweep=false`
不等于关闭连续检测，而是无需查询物理世界。

C++ 的 `update(dt, targets, hits, bodies, worldSweep)` 和
`updateWithMotion(dt, function, targets, hits, bodies, worldSweep)` 接收可选 `ICarrierHitProbe` 世界适配器。
适配器负责世界选择、形状、过滤、物体到带 generation 的实体映射，输出统一接触时间/中心位置/法线。
它在调用期间同步借用，不保存指针；缺少被选中的探针会返回 Unsupported 并回滚整帧，绝不静默退回普通探针。
Squirrel 可通过 `bindWorld3D` 创建此适配器；未启用 physics 或没有绑定时，查询明确失败。
普通几何探针仍可在不包含 physics 的构建中运行。

碰撞是 `onHit` 特殊触发器，响应由 `impacts` 决定；不会自动伤害、反弹或回收。
例如 `impacts = [{ kind = "emitTrigger", on = "onHit" }]` 只产生通知，
事件带 `trigger="onHit"`、`impact="emitTrigger"`、命中时间、位置、法线和目标身份，伤害为零。
也可将 `emitTrigger` 用于定时、距离等触发器。需要伤害、反弹或销毁时显式组合 `emitHit`、`bounce`、`release`。
通知表示一次接触采样，不承诺 enter/stay/exit 去重；持续重叠可能在后续步骤再次通知。
配置随配方编码和原生快照保存；未知碰撞字段或非布尔值拒绝导入。

### Squirrel 绑定 World3D 扫掠

```squirrel
local world = physics.newWorld3D(0.0, 0.0, 0.0, false);
local pool = checked(eve.Weapon().newCarrierRuntime(64));
// 球半径、查询 category、查询 mask、排除的 bodyId；-1 表示全位／无排除。
checked(pool.bindWorld3D(world.queryToken(), 0.12, -1, -1, -1));
// 配方指定 collision={enabled=true,worldSweep=true}，update/updateWithMotion 均自动使用该绑定。
// 池为空时可解除绑定：
checked(pool.clearWorld3D());
```

`checked` 是示例中检查 Result、失败时抛出诊断的辅助函数。绑定失败保持原绑定；活动投射物存在时
禁止更换／解除世界，运动回调内也禁止重入绑定。每个池使用一个世界和一组球形扫掠参数；需要不同
半径／过滤策略时建立独立池。物理查询是球体沿本次提议运动弦线平移，不创建刚体、不施加冲量。
世界先步进，随后推进弹道池；曲线通过 `configureSimulation` 子步近似。相同后端和固定时间步可重放，
不同物理后端按几何容差验证，不承诺逐位一致。

`queryToken()` 是不透明的进程内令牌，含世界身份和恢复代次，不能写入存档。池拥有查询会话而不拥有
物理世界，不跨帧保存 World3D 裸指针。销毁世界、恢复世界或销毁被排除物体后，查询返回失败且不提交
弹道帧。恢复／热重载后应重建空池并用新令牌绑定；池先销毁不影响世界。碰撞关闭时不访问过期会话。

物理事件除了 `trigger/impact/age/x/y/z/nx/ny/nz`，还提供 `bodyId`、`shapeId`、`bodyIndex`、`bodyGeneration`、
`worldIndex`、`worldGeneration`、当前段内 `fraction`。位置是弹体中心，不是物体表面。
未映射到游戏实体的物理接触保持 `targetId=0`，由游戏层按 body 信息映射伤害目标；不会伪造 ECS 身份。
`emitTrigger` 只发通知；`emitHit` 也只是发布伤害事件，不自动对刚体施加伤害。
穿透历史以带代次的 world/body 身份去重，跨子步和原生快照有效。每段超过 256 个 shape 接触时明确失败，
不静默截断；查询不改动世界持久过滤参数，但会覆盖兼容的 shape-cast 结果缓存。

原生 `CarrierRuntimeSnapshot` 已升级为 schema 4，新增 `piercedWorldBodies`；v1/v2/v3 拒绝恢复。
这是同进程强类型快照，不是存档序列化格式。外部迁移需按重建后的物理身份重映射穿透历史，不能只修改版本号。
世界绑定和脚本闭包不进入快照。无新增 ECS System；时间仍由调用者注入，未引入新的随机源。

module-interface 补充：E1/E2 为 physics 提供的可选冷路径工厂和独占查询会话；每池单一、已知提供者、
逐段高频查询、弹道帧整体提交，因此不使用广播事件或运行时字符串服务查找。E3 为物理域权威的
world/body 代次投影及池拥有的穿透历史；E4 为拥有型物理命中来源；E5 为 `queryToken`、`bindWorld3D`、
`clearWorld3D`；E6 无新增工具协议。热路径声明在架构检查器中。

运行示例：[world-sweep-ballistics](../../../examples/world-sweep-ballistics/README.md)。
测试：`test/integration/projectile-behavior/world_sweep.cpp`（Box3D/Jolt 同契约、恢复、生命周期、预算、穿透快照），
`world-script/`（真实 Squirrel 组合）与 `script/`（physics 裁剪时缺失提供者）。

扫掠的初始重叠策略：只报告新穿越；`fraction=0` 的初始重叠／贴面不作为新命中，两个后端使用同一规则。
这也避免反弹后的贴面被重复触发。它不承担穿透恢复／解重叠；生成点应位于障碍外。
