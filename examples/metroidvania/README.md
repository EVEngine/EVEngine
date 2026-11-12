# Momentum Ruins

Physics-driven side-scrolling action demo for EVEngine. The three connected regions teach
combo launching, wall jumping, air dashing, kicking props, momentum damage, and finally a
Dragon boss encounter. Actors are script ECS entities (`Player` / `Enemy` / `Boss` / …)
driven by `eve.System` queries in `ecs.nut`; Box2D remains the physics authority.

Run from the repository root with the platform-specific engine target, for example:

```sh
make run/win32-debug GAME=examples/metroidvania
```

Controls: `A/D` or arrows move, `Space` jumps/double-jumps/wall-jumps, `J` attacks, `K` kicks props, and
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

The built-in Spine loader accepts only its documented subset. When a demo model
uses unsupported features (such as IK or Bezier timelines), the example reports
the unavailable visual and draws a colored, collision-sized actor with a facing
marker. Movement, combat, and enemy behavior remain playable.

## 2026-11-11 视觉改善

修正图集路径必须相对 maps/world.json 解析的问题，避免默认色块替代真实贴图。新增自有程序化石砌/背景素材及兼容角色轮廓；地图碰撞数据保持不变。Spine IK 支持未在本轮扩展，兼容显示没有冒充完整 Spine 动画。通过既有 8 秒移动/跳跃/攻击输入回放的无帧错误检查。

[当前实帧](../../docs/images/examples/review-2026-11-11/next10/metroidvania.png) · [前后对照与验证](../../docs/images/examples/QUALITY_REVIEW.md)

新增细节图集与背景由本例 generate_visuals.py 生成；原有素材及 Kenney CC0 声明保留。重新生成时先运行 generate_visuals.py，再运行 generate_map.py。
