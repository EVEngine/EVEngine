# Cable / Chain / Rope — procedural linear fixtures

Demonstrates the procgen **mesh + PBR texture** recipes for steel cable, iron
chain, and hemp rope. Each mesh is a tileable unit repeated along +X; materials
come from matching `pbr.cable.*` / `pbr.chain.*` / `pbr.rope.*` recipes (no
external image assets).

| Mesh | Texture / PBR | Look |
|------|---------------|------|
| `mesh.cable` | `tex.cable.steel` / `pbr.cable.steel` | 6-strand twisted steel cable |
| `mesh.chain` | `tex.chain.iron` / `pbr.chain.iron` | interlocking oval iron links |
| `mesh.rope`  | `tex.rope.hemp` / `pbr.rope.hemp`   | 3-strand twisted hemp rope |

## Run

```sh
make run/<platform>-debug GAME=examples/cable-chain-rope
```

Headless smoke (Linux VM):

```sh
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a scripts/smoke_examples.sh cable-chain-rope
```

## Controls

- **Tab** — cycle cable / chain / rope
- **+ / -** — grow / shrink segment count
- **R** — reseed texture noise
- Orbit camera spins slowly; pause with the UI toggle when present

## Reusable API

```squirrel
local paramsResult = procgen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setInt("segments", 8);
p.setFloat("segLength", 1.0);
p.setFloat("radius", 0.08);
p.setFloat("thickness", 0.022);
p.setInt("strands", 6);
p.setInt("twists", 1);
local meshResult = procgen.generateMesh("mesh.cable", p, gfx);
if (!meshResult.ok) throw meshResult.status.summary;

local tpResult = procgen.newParams();
if (!tpResult.ok) throw tpResult.status.summary;
local tp = tpResult.value;
tp.setSize(256, 256);
tp.setInt("strands", 6);
tp.setFloat("twist", 3.0);
local pbrResult = procgen.generatePbrMaterial("pbr.cable.steel", tp);
if (!pbrResult.ok) throw pbrResult.status.summary;
```

Shared mesh params: `segments`, `segLength`, `radius`, `thickness`, `strands`,
`twists`, `lengthSegs`, `radialSegs`, `majorSegs`, `minorSegs`, `scale`,
`uvRepeat`. Integer `twists` per segment keeps helical strands seamless at
tile boundaries.

## 2026-11-12 画面改善

钢缆/铁链/麻绳并排展示，补端部支撑与底座，按长度适配镜头；选中项高亮，修正面板位置、文本裁切和键盘改参数后的滑杆同步，保留种子、参数、切换和旋转。

[当前实帧](../../docs/images/examples/review-2026-11-12/nine-improved/cable-chain-rope.png) · [前后与状态对照](../../docs/images/examples/review-2026-11-12/nine-improved/README.md)
