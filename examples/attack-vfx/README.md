# Attack VFX Showcase（综合攻击特效）

贴近参考视频的 arena + 元素攻击演示：棋盘格地面、白色假人、紫色木桩，
按 WATER whip → EARTH cones → FIRE burst → AIR slash 自动轮播。

`eve.StylizeAction()` 用一份 `AttackVfxRecipe` 编排全部 layer role
（meshVfx / trail / particles / camera / distortion / decal / audio / prefab）。
当前 meshVfx / trail / distortion / prefab 多为 CPU 编排占位；**可见冲击**由脚本侧
`Renderable3D` 演出道具 + 粒子（`particles.update` / `render`）+ decal + audio 承担。

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
2. 注册 anticipate→release（`startCue=impact`）配方；anticipate 充能环 + 粒子预热。
3. `signal("impact")` 切入 release：元素演出（鞭 / 锥 / 爆 / 斩）+ 粒子 burst +
   地面 scorch decal + `hit.wav`；配方层事件仍经 `advance` 汇总。
4. 四套 skin：`skin:water` / `skin:earth` / `skin:fire` / `skin:air`，每轮重注册。
5. 可选后端缺失时 runtime soft-skip（`LayerSkipped`），配方仍完整播完。

成功标记：

- 控制台打印 `ATTACK_VFX_READY …`
- 至少两轮 `ATTACK_VFX_PLAY` / `ATTACK_VFX_IMPACT`
- 最终 `ATTACK_VFX_PASS cycles=… plays=… layerStarts=…`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | arena、配方、元素演出、Result 检查、HUD |
| `config.nut` | 1100×700；stylize / stylizeAction / particles / audio / decal / model3d |
| `hit.wav` | release 相位 one-shot 音频 |
| `*_burst.particle.json` | 四元素 + 通用粒子资产 |
| `assets/fonts/DejaVuSans-Bold.ttf` | HUD 标题字体 |
| `README.md` | 本说明 |

## 契约

可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 帧循环契约。
