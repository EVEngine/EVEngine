# Attack VFX Showcase（综合攻击特效）

用一份 `AttackVfxRecipe` 串联全部 layer role：`meshVfx` / `trail` / `particles` /
`camera` / `distortion` / `decal` / `audio` / `prefab`，再通过三套 elemental skin
（FIRE / WATER / LIGHTNING）自动轮播。脚本只依赖 `eve.StylizeAction()` 的
`registerRecipeJson` / `play` / `signal` / `advance` Result API，不写元素分叉代码。

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

1. `eve.StylizeAction()` 注册带 windup→release（`startCue=impact`）两段相位的配方。
2. windup：充能 mesh（`skill:chargeAura`）+ prefab 占位 + 粒子 burst。
3. `signal("impact")` 切入 release：刀光 mesh、trail、camera shake、distortion
   profile、地面 scorch decal、`hit.wav` 一发音频、impact prefab、粒子。
4. 每轮结束后切换 skin（`skin:fire` / `skin:water` / `skin:lightning`）并重注册配方；
   角色方块 tint 跟随 skin。
5. 可选后端缺失时 runtime soft-skip（`LayerSkipped`），配方仍完整播完。

成功标记：

- 控制台打印 `ATTACK_VFX_READY …`
- 至少两轮 `ATTACK_VFX_PLAY` / `ATTACK_VFX_IMPACT`
- 最终 `ATTACK_VFX_PASS cycles=… plays=… layerStarts=…`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 配方拼装、轮播、Result 检查、简易 HUD |
| `config.nut` | 1100×700；裁剪到 stylize / stylizeAction / particles / audio / decal |
| `hit.wav` | release 相位 one-shot 音频 |
| `burst.particle.json` | windup / release 共用的粒子 burst 资产 |
| `README.md` | 本说明 |

## 契约

可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 帧循环契约。
