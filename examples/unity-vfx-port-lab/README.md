# Unity VFX Port Lab（Phase 1–3）

演示从 Unity 开源 VFX **意图重作者**到 `eve.particle-effect` 的配方包：

| 本地文件 | 意图 | 参考源（非资产拷贝） |
|---|---|---|
| `bonfire.effect.json` | 芯焰 + 余烬 + 烟 | Unity VFX Graph Samples Bonfire / Kodrin |
| `portal.effect.json` | 环带吸入 + 核心闪 + 薄雾 | Unity VFX Graph Samples Portal |
| `fireworks.effect.json` | 升空弹壳 → 二级星屑/烟 | keijiro/VfxPyro |
| `fire_flipbook.effect.json` | 序列帧焰舌 + 余烬 | `smoke-flipbook` atlas + Bonfire 意图 |
| `smoke_flipbook.effect.json` | 序列帧烟团 + 细烟 | `smoke-flipbook` 烘焙回放合同 |
| `ribbon_dissipate.effect.json` | 消散丝带 + 死亡薄雾 | VFX Graph Samples Ribbon Pack 意图 |

权威登记、降级与保真度对比见
[`../particle-effects/SOURCES.md`](../particle-effects/SOURCES.md) 与
[`docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md`](../../docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md)。
本目录持有与 `examples/particle-effects/` 同步的 JSON / atlas 副本（游戏 VFS 不能离开示例根目录）。
改权威包后请同步复制到本目录。

## 运行

```bash
cd examples/unity-vfx-port-lab
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/unity-vfx-port-lab
```

成功标记：控制台打印
`UNITY_VFX_PORT_READY bonfire=1 portal=1 fireworks=1 fire_flipbook=1 smoke_flipbook=1 ribbon_dissipate=1`。

## 契约

可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh`。
