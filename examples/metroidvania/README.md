# Momentum Ruins

Physics-driven side-scrolling action demo for EVEngine. The three connected regions teach
combo launching, wall jumping, air dashing, kicking props, momentum damage, and finally a
Dragon boss encounter.

Run from the repository root with the platform-specific engine target, for example:

```sh
make run/win32-debug GAME=examples/metroidvania
```

Controls: `A/D` or arrows move, `Space` jumps/wall-jumps, `J` attacks, `K` kicks props, and
either `Shift` key air-dashes after the upgrade. Enable `debug` in `config.nut` to see Box2D
fixtures.

Run the startup/frame-error smoke test on Windows with:

```powershell
python examples/metroidvania/smoke_test.py build/src/engine/Debug/eve.exe
```

Add `--input-replay` to exercise movement, both jump directions, the three-hit combo, and
the kick action while checking the engine log for frame/runtime errors.

## Asset licenses

- Tile, prop, and background art is selected from Kenney's **Platformer Pack Redux** and is
  CC0. The original license is included at `assets/KENNEY-CC0.txt`; source:
  https://opengameart.org/node/39408
- The animated Hero, Spineboy, and Dragon are loaded directly from `test/assets/spine`.
  Their individual license files remain beside those assets. This demo is non-commercial:
  Hero may not be redistributed and Spineboy may not be used commercially.

The generated map can be reproduced with `python generate_map.py`.
