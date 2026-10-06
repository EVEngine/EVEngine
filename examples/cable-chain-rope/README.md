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
