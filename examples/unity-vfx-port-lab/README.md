# Unity VFX Port Lab（Phase 1）

演示从 Unity 开源 VFX **意图重作者**到 `eve.particle-effect` 的三件配方：

| 本地文件 | 意图 | 参考源（非资产拷贝） |
|---|---|---|
| `bonfire.effect.json` | 芯焰 + 余烬 + 烟 | Unity VFX Graph Samples Bonfire / Kodrin |
| `portal.effect.json` | 环带吸入 + 核心闪 + 薄雾 | Unity VFX Graph Samples Portal |
| `fireworks.effect.json` | 升空弹壳 → 二级星屑/烟 | keijiro/VfxPyro |

权威登记与降级说明见
[`../particle-effects/SOURCES.md`](../particle-effects/SOURCES.md) 与
[`docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md`](../../docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md)。
本目录持有与 `examples/particle-effects/` 同步的 JSON 副本（游戏 VFS 不能离开示例根目录）。
改权威包后请同步复制三份文件到本目录。

## 运行

```bash
cd examples/unity-vfx-port-lab
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/unity-vfx-port-lab
```

成功标记：控制台打印 `UNITY_VFX_PORT_READY bonfire=1 portal=1 fireworks=1`。

## 契约

可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh`。
