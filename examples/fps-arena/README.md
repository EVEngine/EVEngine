# FPS Arena — Phase A 竖切

第一人称射击组装示例：相对鼠标看、胶囊移动、武器开火事件 → `World3D.rayCast` →
`CombatRuntime.applyDamage`。武器模块只发事件，不结算生命值。

对应计划：[`docs/dev/2026-11-06-fps-vertical-slice-plan.md`](../../docs/dev/2026-11-06-fps-vertical-slice-plan.md) Phase A。

## 运行

```bash
make run/<platform>-debug GAME=examples/fps-arena
```

Headless smoke：

```bash
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh fps-arena
```

## 操作

| 输入 | 作用 |
|------|------|
| 鼠标移动 | 第一人称视角（启动时尝试捕获；Esc 切换） |
| WASD | 移动 |
| Space | 跳跃 |
| 鼠标左键 / F | 开火（自动步枪） |
| 鼠标右键 / Q | ADS（套用武器 `zoomFov`） |
| R | 装填 |

## 组装路径

1. `CameraController` `firstperson` + 脚本维护 `lookYaw`/`lookPitch`（控制器无 getYaw API）
2. `World3D.moveCapsule` 解析位移；重力 + 落地检测
3. `eve.Weapon()` 注册 hitscan 步枪；`fireAt` 只产生 `fire` 事件与后坐力
4. 消费事件时用眼点沿视角 `rayCast`；命中靶刚体则 `combat.applyDamage`
5. HUD：准星 + 弹药 + HP + 靶数（2D `drawSolidRect` / `drawText`）

启动约 0.5s 后自动对中央靶开一枪（`FPS_ARENA_PASS`），方便无头 smoke 验证伤害链路。

## 刻意不做（后续 Phase）

冲刺/蹲下、viewmodel、配件、敌人 AI、弹孔 decal（见计划书 B–F）。
