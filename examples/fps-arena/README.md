# FPS Arena — Phase A–C 竖切

第一人称射击组装示例：相对鼠标看、胶囊移动（Walk / Sprint / Crouch）、武器开火事件 →
`World3D.rayCast` → `CombatRuntime.applyDamage`，以及墙面/靶子命中标记。

对应计划：[`docs/dev/2026-11-06-fps-vertical-slice-plan.md`](../../docs/dev/2026-11-06-fps-vertical-slice-plan.md)。

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
| 鼠标移动 | 第一人称视角（Esc 切换捕获） |
| WASD | 移动 |
| Shift | 冲刺（着地且非蹲） |
| Ctrl / C | 蹲下（低矮遮挡下松开仍保持，直至 `canStand`） |
| Space | 跳跃（蹲下时禁用） |
| 鼠标左键 / F | 开火 |
| 鼠标右键 / Q | ADS（`zoomFov`） |
| R | 装填 |
| B | 开关 head bob |

## 命中权威时序

```text
input → weapon.fireAt / update
     → WeaponEvent "fire" (+ recoil / ammo)
     → eye rayCast (game)
     → impact mark (wall vs target)
     → combat.applyDamage (HP owner)
     → camera recoils from event pitch/yaw
```

武器模块不结算生命值；本示例在消费 `fire` 事件时完成射线与伤害。

## 步态契约

脚本 `evaluateGait` 镜像 C++ `eve::combat::FpsGait`（`src/modules/combat/FpsGait.h`）：

- 优先级：Crouch > Sprint > Walk
- Sprint 需要 grounded
- Crouch 在 `!canStand` 时闩锁（站起胶囊被顶挡住）
- 输出：`moveSpeed` / `capsuleHeight` / `eyeHeight`

推荐鼠标灵敏度：`lookSens = 0.12`（度/像素相对位移），俯仰钳制 `[-89, 89]`。

## 刻意不做（后续 Phase）

Viewmodel 双 FOV、配件槽、敌人 AI、NavMesh（见计划 D–G）。
