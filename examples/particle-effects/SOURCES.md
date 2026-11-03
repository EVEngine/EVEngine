# particle-effects 来源与许可证

本目录是 EVEngine **原生**参考配方包（`eve.particle-effect`）。
从 Unity 开源 VFX 移植时的效果必须按
[`docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md`](../../docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md)
登记到下表，并写明降级项。

## 现有资产

| 文件 | Class | Source | License | Notes |
|------|-------|--------|---------|-------|
| `fire.effect.json` | C | native EVEngine | repo | 芯焰 + 余烬；intensity 参数 |
| `smoke.effect.json` | C | native EVEngine | repo | 循环烟 |
| `impact.effect.json` | C | native EVEngine | repo | core/sparks + death→smoke `eventRoutes` |
| `trail.effect.json` | C | native EVEngine | repo | 拖尾向 |
| `weather.effect.json` | C | native EVEngine | repo | 环境向 |

## 移植登记（Phase 1+）

<!-- 每增加一个 Unity 启发配方，追加一行工作单摘要： -->
<!-- | bonfire.effect.json | C | url@commit | SPDX | Downgrades: … | -->

（尚无 Unity 移植条目；首批计划：`bonfire` / `portal` / `fireworks`。）
