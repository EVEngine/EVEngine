# Unit surface effects: reference and shader acceptance

The contact sheet is AI-generated concept art, not engine output. Its role is
palette, motif, contrast and animation-direction guidance. `preview*.png` in the
example are engine-owned presented-frame captures and are named separately.

## Reference generation prompt

Built-in imagegen, stylized-concept; landscape 7-column by 4-row contact sheet.
26 identically posed gray robot busts on a dark navy studio background, plus a
neutral baseline and legend. Preserve visible base material. Surface overlays
hug each mesh; no large particles, floating shields or floor circles. Each cell
has its numbered English name. Use the following exact ordered art directions.
Header: UNIT SURFACE EFFECTS - 26 SHADER REFERENCES.

| # | Preset | 中文 | Surface motif and animation |
|---|---|---|---|
| 01 | shield | 护盾 | Cyan hex grid, pulsing Fresnel rim |
| 02 | hit | 受击 | White-orange fast flash and decay |
| 03 | frost | 冰霜 | Ice-blue crystalline veins, travelling glints |
| 04 | burn | 灼烧 | Orange molten cracks flowing upward |
| 05 | poison | 毒蚀 | Lime drifting corrosive islands |
| 06 | heal | 治疗 | Mint ascending bands and small crosses |
| 07 | shock | 电击 | Branching blue-white surface bolts |
| 08 | petrify | 石化 | Gray stone islands, dark fracture edges |
| 09 | stealth | 隐身提示 | Faint teal rim and stippled camouflage shimmer |
| 10 | phase | 相位 | Violet broken horizontal bands |
| 11 | cleanse | 净化 | White-gold downward wash and speckles |
| 12 | curse | 诅咒 | Purple rune-like angular lattice |
| 13 | berserk | 狂暴 | Red vascular streaks and heartbeat pulses |
| 14 | holyArmor | 神圣护甲 | Gold geometric plate borders |
| 15 | shadow | 暗影侵蚀 | Dark violet creeping wisps |
| 16 | charge | 能量充能 | Blue circuit paths converging toward center |
| 17 | overheat | 过热 | Orange hot-metal gradient and vent stripes |
| 18 | wet | 潮湿 | Blue vertical rivulets with moving highlights |
| 19 | sand | 流沙 | Ochre granular swirling erosion |
| 20 | metallize | 金属化 | Silver angular facets and travelling reflection bands |
| 21 | bark | 木质化 | Brown vertical grain ridges and green seams |
| 22 | fracture | 裂解 | Magenta fragment borders around dark islands |
| 23 | scan | 扫描标记 | Cyan scan line and technical grid |
| 24 | teleport | 传送 | Cyan/violet ascending pixel breakup |
| 25 | slowTime | 时间减速 | Blue contour ripples with delayed phase |
| 26 | lifesteal | 吸血 | Crimson streams converging toward center |

## Implementation contract

- Stable local coordinates, common color/intensity/opacity/time/speed/scale,
  per-effect defaults; additional edgeWidth, distortion, seed and progress controls.
- Additive light motifs; alpha composite material coatings and dark motifs.
- Shared procedural building blocks are allowed; merely recoloring one noise
  pattern does not satisfy different motifs.
- No gameplay mutation: `stealth` is an invisibility-state surface indicator, not
  actual world invisibility. `teleport`/`fracture` are surface breakup indicators;
  a gameplay owner must drive the base material visibility separately.
- Metal/wet effects approximate coating highlights; they do not add a second PBR,
  scene-color refraction, skeletal deformation or a reflection-capture system.
- Render with the same mesh/transform as the target. Static primitives supported;
  skinned targets need their deformed mesh. Caller owns clock and target lifecycle.
- Judge silhouette attachment, motif, palette and temporal change against concept;
  generated lighting is artistic guidance, not a pixel-exact shader contract.
