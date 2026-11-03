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
| `textures/smoke-flipbook-atlas.png` | C | `examples/smoke-flipbook` 烘焙便览副本 | repo | 6×6 / 128px / 36 帧；引擎自生成，非 Unity 贴图 |

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

`examples/attack-vfx/` 持有本包配方的本地副本，并新增共享
`aftermath_smoke.effect.json`（短促 plume + sparks，供 aftermath 相位）。
Skin 映射见该示例 `README.md`。

## 保真度增强（Phase 3）

| 文件 | Class | Source | License | Downgrades / Notes |
|------|-------|--------|---------|-------------------|
| `smoke_flipbook.effect.json` | C | Playback contract from `examples/smoke-flipbook` (6×6 @ 18 fps); soft-plume layering intent from Samples / Kodrin smoke | repo atlas + original JSON | No fluid solver at runtime; no Unity texture sheet; life capped ≈2s to avoid flipbook wrap |
| `fire_flipbook.effect.json` | C | Same atlas + Bonfire/Kodrin fire tint intent; **reauthored** | repo atlas + original JSON | Atlas is smoke bake tinted additive — not a dedicated fire sheet; no mesh logs / Shader Graph |
| `ribbon_dissipate.effect.json` | C | Intent from VisualEffectGraph-Samples Ribbon Pack dissipating strips; **reauthored** | Unity Companion for *ideas only*; JSON original | No VFX Graph ribbon output; distance emission + `setRibbon`; death→mist via CPU `eventRoutes`; needs moving emitter head |

### Worksheets

#### smoke_flipbook

- Class: C
- Source: EVEngine `smoke-flipbook` bake + Unity smoke layering intent
- Intent: soft animated smoke puff (texture sheet) + thin wisps
- Layers: `puff` flipbook alpha, `wisps` soft sprites
- Events / Timeline: timed puff emits on a 2.4s loop
- Sim path: gpu-candidate (flipbook + basic motion); gradients may keep CPU
- Texture: `textures/smoke-flipbook-atlas.png`
- Verification: port-lab + pack render test

#### fire_flipbook

- Class: C
- Source: same atlas; Bonfire flame tongue intent
- Intent: additive flipbook flame tongue + rising embers
- Layers: `tongue` flipbook additive, `embers` velocity-stretch
- Events / Timeline: staggered tongue emits + intensity pulse
- Sim path: cpu-friendly (gradients); flipbook itself is GPU-capable
- Verification: same as smoke_flipbook; AttackVfx FIRE charge

#### ribbon_dissipate

- Class: C
- Source: VisualEffectGraph-Samples Ribbon Pack (intent)
- Intent: moving ribbon that fades along life, shedding mist on death
- Layers: `ribbon`, `sparks`, `mist`
- Events / Timeline: intensity breathe; death→mist route
- Sim path: cpu (eventRoutes + color gradient); ribbon GPU-capable alone
- Verification: port-lab moves the effect head each frame

## Phase 3 保真度对比表

| 效果 | 意图 | 降级 | 截帧 / 证据 |
|------|------|------|-------------|
| smoke_flipbook | 序列帧烟团升起 | 无运行时流体；贴图为引擎自烘焙 smoke atlas | port-lab 底排中；`UNITY_VFX_PORT_READY … smoke_flipbook=1` |
| fire_flipbook | 序列帧焰舌 + 余烬 | 烟图集染色代替专用火图；无 3D mesh | port-lab 底排左；AttackVfx FIRE charge |
| ribbon_dissipate | 消散丝带 + 死亡薄雾 | 非 Graph strip；需脚本移动发射头 | port-lab 底排右（绕圈运动） |
| MeshVfx | — | **未新增**（2D 粒子足够） | — |

仍无 VFX Graph 运行时 / `.vfx` / HLSL 导入。
