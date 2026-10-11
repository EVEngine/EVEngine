# Keyboard

[中文](../../../usr/modules/keyboard.md) · [Module handbook](../MODULES.md)

**Script constructor:** `eve.Keyboard()`. The usual game instance is `keyboard`.

```squirrel
if (keyboard.isDown("left")) playerX -= speed * dt;
if (keyboard.isDown("space")) jump();
```

`playerX`, `speed`, `dt` and `jump()` belong to your game. Run input logic in `eve_update`.

## Continuous motion and one-time actions

`isDown(key)` reports held state, not a pressed/released edge. Use it directly for continuous movement. For a jump or menu confirmation, save the previous frame's state and trigger on `down && !wasDown`; holding a key should not trigger the action every frame.

Key names follow keyboard layout; scancodes describe physical keys. Choose intentionally rather than mixing them.

## Text input

Enable text input when an input field gains focus and disable it when leaving the field. Let the UI/text-input path consume text. Do not assemble user text from key names: keyboard layouts, composed characters and input methods will fail. Leaving text input enabled can keep a mobile keyboard visible.

See [API conventions](../API-CONVENTIONS.md) for exact bindings, defaults and return values. [Source](../../../../src/modules/keyboard/).
