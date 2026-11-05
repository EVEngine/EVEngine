# Enemy Vision — 锥形视野 + 墙体遮挡 + 感知记忆

演示「给每个敌人加视野」的引擎推荐路径：游戏宿主镜像位姿到 `SensingWorld`，
对每个敌人跑 `QueryCone`，再用**宿主侧 World2D 视线**对障碍 AABB 做遮挡判定，
命中后 `NpcAiWorld.remember(..., sense="vision")`，行为图用
`perception.vision` / `perception.forgotten.vision` 在 `patrol` ↔ `chase` 之间切换。
仇恨表（`aggro`）留在游戏侧。

> 引擎尚未提供开箱的 World2D `ILineOfSightQuery`（physics 只认领 World3D）。
> 本示例把墙体 LOS 写在宿主里，并让锥体绘制在墙处截断，方便肉眼确认遮挡。
> 正式项目可实现并 `ensureLineOfSightRouter()->addProvider(World2D, …)`，再在
> query JSON 里开 `"lineOfSight":true`。

## 运行

```bash
make run/<platform>-debug GAME=examples/enemy-vision
```

或：

```bash
cd examples/enemy-vision && ../../build/linux-debug/src/engine/eve run
```

操作：`WASD` / 方向键移动。走进守卫锥体且**无墙遮挡**会追击；躲到墙后同一锥体内
只会看到 `(blocked)`，不会进入 `chase`。

## 管线

1. `upsert` 玩家与守卫到共享 `SensingWorld`。
2. 每敌人 `world.query(eve.sensing.query JSON)` 做锥形候选。
3. 宿主 `evHasLineOfSight` 对 `obstacles` AABB 做段检测；被挡则 `blocked=true`，不 `remember`。
4. 通视 → `setBlackboardString(target)` + `remember(sense="vision")` + 写入 `aggro`。
5. 不通视 / 离开锥体 → 不刷新 remember；过期发出 `perception.forgotten.vision`。
6. `NpcAiWorld.tick` 切换状态；宿主按 `activeState` 巡逻/追击，并与墙体做圆-AABB 分离。

启动时 `verifyPipeline()` 先测通视追击，再把玩家放到 `wall.mid` 西侧（仍在锥内）确认
回到 `patrol` 且 `blocked=true`，打印 `ENEMY_VISION_READY ... verify=pass`。

## 文件

| 文件 | 职责 |
|---|---|
| `config.nut` | 窗口 + `sensing` / `npc_ai` / `keyboard` / `font` |
| `behavior.nut` | `enemy.vision` BehaviorDefinition JSON |
| `simulation.nut` | 障碍、锥查询、LOS、记忆、aggro、位移 |
| `main.nut` | 输入、墙/锥可视化（锥体在墙处截断） |
| `README.md` | 本说明 |

## 相关

- 用户手册：[sensing](../../docs/usr/modules/sensing.md)、[npc_ai](../../docs/usr/modules/npc_ai.md)
- 设计边界：`docs/dev/感知与目标选择系统设计.md`（sensing 不出 primary / threat）
