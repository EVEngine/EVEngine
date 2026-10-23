# Window

[中文](../../../usr/modules/window.md) · [Module handbook](../MODULES.md)

**Script constructor:** `eve.Window()`. The runtime normally initializes the game window; game code uses the injected `win` instance rather than creating a second window.

```squirrel
print(win.getWidth() + "x" + win.getHeight() + "\n");
```

## Creation and resizing

Set width, height and title in `config.nut`. Native startup associates Graphics before applying `WindowSettings`. In an options menu, call `setSize(width, height)`, read the actual size back and update camera viewport, Canvas targets and UI layout. Mobile platforms may not honor a requested size. Use `close()` for a quit action instead of abruptly terminating the process.

## High DPI

`getWidth()` / `getHeight()` are logical window dimensions; use them for normal camera/UI coordinates. `getPixelWidth()` / `getPixelHeight()` are actual swapchain pixel dimensions, useful for captures and pixel-sized targets. `getDPIScale()` is the pixel-to-logical width ratio. Choose one coordinate space; do not use pixel dimensions and multiply by DPI again.

## Troubleshooting

- Applying settings can fail if Graphics is not associated or the mode is unsupported.
- Trust dimensions read back from the window, especially on mobile.
- A stretched image after resizing usually means the camera/Canvas/UI still uses old dimensions.
- `hasFocus()` reports input focus on the window thread; use it to avoid background edge scrolling.

See [API conventions](../API-CONVENTIONS.md) for signatures and script-binding lookup. [Source](../../../../src/modules/window/).
