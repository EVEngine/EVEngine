# Mouse

[中文](../../../usr/modules/mouse.md) · [Module handbook](../MODULES.md)

**Script constructor:** `eve.Mouse()`. The usual game instance is `mouse`.

```squirrel
local mx = mouse.getX();
local my = mouse.getY();
if (mouse.isDown(1)) fireAt(mx, my);
```

`fireAt` is your game's function. Coordinates are in window space; transform them with the current camera before testing world objects. Mouse buttons use integer indices, not keyboard names. Use `ui.wantCaptureMouse()` to avoid sending UI interactions to gameplay.

## Cursor visibility and relative motion

Use `setVisible(bool)` / `isVisible()` for visibility. For first-person control:

```squirrel
local enabled = mouse.setRelativeMode(true);
if (!enabled) print("relative mouse mode was rejected\n");
// Restore absolute mode when leaving first-person control.
mouse.setRelativeMode(false);
```

SDL can reject relative mode when a window lacks focus. Check the return value. Do not repeatedly warp the pointer to the center and subtract `getX()` values; on Windows the warp can create reverse movement and make the camera snap back.

During `eve_update`, read `getMovementX()` **before** `getMovementY()`: the X read fetches relative motion and latches Y. `setGrabbed(bool)` controls window capture; behavior differs by platform.

## Wheel snapshots

`getWheelX()` / `getWheelY()` report accumulated deltas from the latest completed event pump. Direction is normalized for flipped events; newer SDL versions preserve precise scrolling. Multiple reads in a frame do not consume the input. The next pump with no wheel events clears the values. Read on the event thread during update. UI-captured events are excluded from the gameplay snapshot.

See [API conventions](../API-CONVENTIONS.md). [Source](../../../../src/modules/mouse/).
