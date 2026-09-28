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

## API 快查

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
| `cast(...)` / `castAtTarget(...)` / `castJson(...)` | 编译 + 注册 + 生成；含 homing/proximity 时必须 `castAtTarget`。 |
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
