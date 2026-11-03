# particle-effects 来源与许可证

本目录是 EVEngine **原生**参考配方包（`eve.particle-effect`）。
从 Unity 开源 VFX 移植过来的效果按
[`docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md`](../../docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md)
登记到下表，并写明降级项。**只移植数值/编排意图，不拷贝 Unity 贴图或 `.vfx` 资产。**

## 现有资产

| 文件 | Class | Source | License | Notes |
|------|-------|--------|---------|-------|
| `fire.effect.json` | C | native EVEngine | repo | 芯焰 + 余烬；intensity 参数 |
| `smoke.effect.json` | C | native EVEngine | repo | 循环烟 |
| `impact.effect.json` | C | native EVEngine | repo | core/sparks + death→smoke `eventRoutes` |
| `trail.effect.json` | C | native EVEngine | repo | 拖尾向 |
| `weather.effect.json` | C | native EVEngine | repo | 环境向 |

## 移植登记（Phase 1）

| 文件 | Class | Source | License | Downgrades / Notes |
|------|-------|--------|---------|-------------------|
| `bonfire.effect.json` | C | Intent from [Unity-Technologies/VisualEffectGraph-Samples](https://github.com/Unity-Technologies/VisualEffectGraph-Samples) Bonfire + [Kodrin/VFX-Essentials](https://github.com/Kodrin/VFX-Essentials) layered fire; **reauthored** | Unity Companion / MIT for *ideas only*; JSON is original EVEngine | No VFX Graph / Shader Graph fire shader; no mesh logs; 2D billboards + noise; CPU-friendly curves |
| `portal.effect.json` | C | Intent from VisualEffectGraph-Samples Portal; **reauthored** | Unity Companion for *ideas only*; JSON original | No SDF portal mesh; suction via `radialAcceleration` + `ring` area (not world-space forceFields); mist is alpha sprites |
| `fireworks.effect.json` | C | Intent from [keijiro/VfxPyro](https://github.com/keijiro/VfxPyro); **reauthored** | Check upstream LICENSE for *ideas*; JSON original | No GPU Event; shell→stars/smoke via CPU `eventRoutes`; no custom pyro shader |

### Worksheets

#### bonfire

- Class: C
- Source: VisualEffectGraph-Samples Bonfire + Kodrin layered fire (intent)
- Intent: looping campfire = core flame + rising embers + smoke plume
- Layers: `core` additive, `embers` velocity-stretch, `smoke` alpha
- Events / Timeline: intensity pulse cues; no eventRoutes
- Sim path: cpu (noise + multi-stop color gradients)
- Verification: `examples/unity-vfx-port-lab` + pack parse test

#### portal

- Class: C
- Source: VisualEffectGraph-Samples Portal (intent)
- Intent: elliptical ring sucked inward with bright core and soft mist
- Layers: `ring`, `core`, `mist`
- Events / Timeline: intensity pulse; radial/tangential accel for swirl
- Sim path: gpu-candidate if curves trimmed; default authored with gradients → cpu
- Verification: same as bonfire

#### fireworks

- Class: C
- Source: keijiro/VfxPyro (intent)
- Intent: ascending shell bursts into colored stars + light smoke
- Layers: `shell`, `stars`, `smoke`
- Events / Timeline: timed `emit` on shell; death → stars/smoke routes
- Sim path: cpu (eventRoutes / sub-emitters)
- Verification: same as bonfire

## AttackVfx 复用（Phase 2）

`examples/attack-vfx/` 持有本包三配方的本地副本，并新增共享
`aftermath_smoke.effect.json`（短促 plume + sparks，供 aftermath 相位）。
Skin 映射见该示例 `README.md`。
