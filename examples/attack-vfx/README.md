# Attack VFX Showcase（综合攻击特效）

贴近参考视频的 arena + 元素攻击演示：棋盘格地面、白色假人、紫色木桩，
按 WATER whip → EARTH cones → FIRE burst → AIR slash 自动轮播。

`eve.StylizeAction()` 用一份 `AttackVfxRecipe` 编排全部 layer role
（meshVfx / trail / particles / camera / distortion / decal / audio / prefab）。
当前 meshVfx / trail / distortion / prefab 多为 CPU 编排占位；**可见冲击**由脚本侧
`Renderable3D` 演出道具 + 粒子（`particles.update` / `render`）+ decal + audio 承担。

## Phase 2：共享 Unity-intent 粒子配方

粒子层按相位引用共享 `eve.particle-effect` 资产（移植方案 Phase 1），而不是每层手写重复 burst：

| Skin | anticipate（充能） | release（冲击） | aftermath（残留） |
|---|---|---|---|
| WATER | `portal.effect.json` | `water_burst.particle.json` | `aftermath_smoke.effect.json` |
| EARTH | `bonfire.effect.json` | `earth_burst.particle.json` | `aftermath_smoke.effect.json` |
| FIRE | `bonfire.effect.json` | `fireworks.effect.json` | `aftermath_smoke.effect.json` |
| AIR | `portal.effect.json` | `air_burst.particle.json` | `aftermath_smoke.effect.json` |

`bonfire` / `portal` / `fireworks` 与 `examples/particle-effects/` 同步（游戏 VFS 限制，本目录持副本）。
出处与降级见该包 `SOURCES.md` 与
[`docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md`](../../docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md)。

## 运行

```bash
cd examples/attack-vfx
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/attack-vfx
```

## 演示内容

1. 棋盘格地板 + 白假人（持杖）+ 紫色圆柱木桩（白顶 / 眼睛）。
2. 注册 **anticipate → release（`startCue=impact`）→ aftermath** 配方。
3. `signal("impact")` 切入 release：元素演出 + 粒子 + scorch decal + `hit.wav`。
4. aftermath 播放共享残留烟/火星；粒子层 `stopBehavior=stopEmitting`。
5. 四套 skin 每轮重注册；缺后端时 soft-skip。

成功标记：

- `ATTACK_VFX_READY …`
- `ATTACK_VFX_PLAY` / `ATTACK_VFX_IMPACT` / `ATTACK_VFX_AFTERMATH`
- `ATTACK_VFX_PASS cycles=… plays=… layerStarts=…`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | arena、配方、元素演出、Result 检查、HUD |
| `config.nut` | 1100×700；stylize / stylizeAction / particles / audio / decal / model3d |
| `hit.wav` | release 相位 one-shot 音频 |
| `bonfire/portal/fireworks.effect.json` | Phase 1 共享配方副本 |
| `aftermath_smoke.effect.json` | 共享残留相位 |
| `*_burst.particle.json` | 元素向冲击（earth/air/water；fire 已改用 fireworks） |
| `assets/fonts/DejaVuSans-Bold.ttf` | HUD 标题字体 |

## 契约

可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh`。
