# Graphics: Getting Started

[中文完整手册](../../../usr/modules/graphics.md) · [Module handbook](../MODULES.md)

This manually maintained English introduction covers everyday drawing and resource usage. The authoritative Chinese guide contains advanced lighting, HDR, reflection-probe and effect configuration.

**Script constructor:** `eve.Graphics()`. The normal game instance is `gfx`. Graphics owns the device, swapchain, cameras and submission. Textures, meshes, shaders and renderables are resources or scene objects: create them during initialization and update their parameters during frames.

## Draw a 2D frame

```squirrel
eve_init <- function() {
    gfx.setBackgroundColor(0.08, 0.1, 0.16, 1.0);
};
eve_render <- function() {
    gfx.clear();
    gfx.drawSolidRect(40, 40, 160, 80, 0.2, 0.7, 1.0, 1.0);
};
```

Clear once at the start of `eve_render`, then submit background, map, actors, particles and UI in the intended order. The engine's normal loop handles presentation. Use `drawTexturedRect()` for textures. `drawTexturedRectRotated()` rotates around the rectangle center; positive degree angles look clockwise in screen coordinates with Y pointing down.

For text, `drawText(font, text, x, y, r, g, b, a, scale)` uses a font returned by `newFont()` and places the line's top-left corner at `(x, y)`. It does not change current-font state. The existing `print(...)` path uses the font selected with `setFont()`.

## Persistent sprites

Use Sprite2D for UV animation, rotation, independent blending and transforms:

```squirrel
local sprite = gfx.newSprite2D();
sprite.setTexture(sheet.getTexture());
sprite.setNormalTexture(normalMap);
sprite.setQuad(quad);
sprite.setPosition(400, 270);
sprite.setScale(2.0, 2.0);
sprite.setRotation(30.0);
sprite.setAnchor(0.25, 0.75);
sprite.setFlip(true, false);
sprite.setReceiveLight(true);
sprite.setBlend("alpha");

// In eve_render, after clearing and drawing the background:
gfx.renderSprites();
```

`sheet`, `normalMap` and `quad` are resources created by your game. Blend choices are `alpha`, `premultiplied`, `additive` and `multiply`. Sprite animation can bind to the sprite to apply trimmed-frame offsets automatically. Call `destroy()` when finished. Do not also enqueue the same sprite through another 2D path or it will render twice.

GPU lit2d requires albedo plus a tangent-space normal texture, lighting enabled and no custom 2D shader. It works with Light2D and Camera2D ambient lighting, with at most eight lights per Canvas. Albedo-only sprites use approximate CPU modulation. Lit normals rotate with sprite geometry. Tilemap lighting is opt-in through `layer.setReceiveLight(true)`.

## Cameras, picking and 3D

Mouse input is in window coordinates. Use Camera2D/Camera3D screen/world conversion or picking rays before testing world objects. General shape tests belong to Math; body queries belong to Physics.

Create meshes, shaders and renderables during initialization. Configure the camera, ambient light and directional/local lights, update transforms/materials during frames, then call `render3D()`. Tune shadow bias and strength per scene. Spot angles are outer half-angles in degrees, not full cone angles.

Keep coordinate spaces consistent after window resizing or when drawing to a Canvas. See the [DPI guide](window.md). Resource creation and disk loading should not happen repeatedly in the render callback.

## Rendering boundaries in 0.6

- Forward+ and hybrid lighting are selectable; transparent objects remain Forward+. Software Vulkan/CI defaults to Forward+; full reference-scene pixel parity is not established by a successful build.
- Vulkan supports SDR/auto/HDR10/scRGB output selection and falls back to SDR without an HDR surface. WebGPU supports SDR/auto. Real HDR-display end-to-end acceptance is still pending.
- Reflection probes support capture, scheduling, sky backup and camera binding. Per-pixel sky/atmosphere capture remains incomplete. Missing Vulkan compute-filter shaders report Unsupported; unfiltered staging data must not be treated as published reflection data.
- FXAA is available through the anti-aliasing path; complete hardware MSAA, SMAA LUT passes and TAA remain further work.
- Some resource-shader/instance paths are Vulkan-only. Unsupported backends report structured failure rather than silently emulating success.
- Scene tone mapping accepts `none`, `aces` and `filmic`, with `aces` the default. Unsupported switching preserves the old mode. Final display mapping does not pre-map linear HDR reflections/captures to SDR.
- Photographic vignette accepts intensity `[0,1]`, default `0`. Invalid/unsupported choices preserve the current setting; unsupported backends accept only `0`. It is separate from transition vignette.

Check Result returns before using the value. Exact signatures belong in [API conventions](../API-CONVENTIONS.md), [Doxygen](https://evengine.github.io/EVEngine/) and your SDK's binding catalog. Advanced effects remain in the [Chinese rendering guide](../../../usr/modules/graphics/rendering-effects.md).
