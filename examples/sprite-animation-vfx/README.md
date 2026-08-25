# Sprite animation VFX API test

End-to-end Squirrel example for the 2D sprite-animation API. It loads 64
independent 128×128 RGBA PNG files into one runtime atlas and renders four
instances with curve-driven speed, 1×, 2×, and reverse playback. Each instance also receives
programmatic translation, center rotation, and pulsing scale; the sprites use
alpha blending because the source stores white RGB in fully transparent pixels;
this also exercises the blend setter without producing additive white fringes.

```powershell
make run/win32-debug GAME=examples/sprite-animation-vfx
```

The test covers:

- `anim.newSpriteSheetFromSequence(..., "assets/frame_{n}.png", 1, 64, 8)`;
- `SpriteClip.addRange` / `setFPS`;
- positive and negative playback (`play` / `playReverse`);
- piecewise-linear speed curves with independent looping;
- loop event consumption;
- `gfx.newSprite2D` transform, pivot, UV flip, Quad, texture and blend setters;
- `gfx.renderSprites` without an extra present.

The PNG sequence was supplied by the user for this engine test. Confirm the
original asset pack's redistribution license before publishing or shipping it.
