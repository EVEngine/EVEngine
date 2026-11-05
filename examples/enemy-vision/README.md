# Enemy Vision — 每敌人锥形视野 + 感知记忆

演示「给每个敌人加视野」的引擎推荐路径：游戏宿主镜像位姿到 `SensingWorld`，
对每个敌人跑 `QueryCone`，命中后 `NpcAiWorld.remember(..., sense="vision")`，
行为图用 `perception.vision` / `perception.forgotten.vision` 在 `patrol` ↔ `chase`
之间切换。仇恨表（`aggro`）留在游戏侧，不放进 sensing。

## 运行

```bash
make run/<platform>-debug GAME=examples/enemy-vision
```

或：

```bash
cd examples/enemy-vision && ../../build/linux-debug/src/engine/eve run
```

操作：`WASD` / 方向键移动玩家（绿色方块）。走进某守卫的半透明锥体后，
该守卫变红并追击；离开锥体一小段时间后记忆过期，回到巡逻。

## 管线

1. `upsert` 玩家与守卫到共享 `SensingWorld`（事实镜像，不是权威根）。
2. 每敌人自定义 `halfAngle` / `range`，用 `world.query(eve.sensing.query JSON)` 做锥形查询。
3. 命中 → `setBlackboardString(target)` + `remember(sense="vision", forgetAfterTicks=2)`，并写入游戏 `aggro`。
4. 未命中 → **不**刷新 remember；下次 `tick` 过期时发出 `perception.forgotten.vision`（显式 `forget()` 不会发该信号）。
5. `NpcAiWorld.tick` 应用行为转换；宿主按 `activeState` 执行巡逻/追击位移。

本示例刻意不接几何 LOS：2D 场景没有开箱 World2D LOS provider。需要遮挡时，
在 3D 路径注册 physics World3D LOS，或自写 World2D `ILineOfSightQuery` 后在 query JSON 里设 `"lineOfSight":true`。

## 文件

| 文件 | 职责 |
|---|---|
| `config.nut` | 窗口 + `sensing` / `npc_ai` / `keyboard` / `font` |
| `behavior.nut` | `enemy.vision` BehaviorDefinition JSON |
| `simulation.nut` | Sensing 镜像、锥查询、记忆、aggro、位移 |
| `main.nut` | 输入、固定 60Hz 步进、锥体可视化 |
| `README.md` | 本说明 |

## 相关

- 用户手册：[sensing](../../docs/usr/modules/sensing.md)、[npc_ai](../../docs/usr/modules/npc_ai.md)
- 设计边界：`docs/dev/感知与目标选择系统设计.md`（sensing 不出 primary / threat）
