# Iron Chain Bridge（铁锁桥）

A small scenic demo that composes the new cable / chain / rope procgen recipes into a
simple iron chain bridge:

| Part | Recipe |
|------|--------|
| Main suspension chains (left / right) | `mesh.chain` + `pbr.chain.iron` |
| Deck planks | `mesh.bridge` + wood tint |
| Handrail ropes | `mesh.rope` + `pbr.rope.hemp` |
| Anchor posts | box meshes with iron PBR |

No external image assets — everything is generated at load time.

## Run

```sh
make run/<platform>-debug GAME=examples/iron-chain-bridge
```

Headless smoke:

```sh
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a scripts/smoke_examples.sh iron-chain-bridge
```

## Controls

- Orbit camera spins slowly
- **R** — reseed chain / rope materials
- **+/-** — grow / shrink chain segment count (bridge span density)

Success marker: console prints `IRON_CHAIN_BRIDGE_PASS chains=… planks=…`.
