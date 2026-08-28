# Combat Action Editor example

This example composes EVEngine's UI-neutral editor SDK into a project-specific
combat action editor. It uses the canonical `eve.action.timeline` asset model,
native hit testing and drag preview, one-step transactions, undo/redo, and
deterministic preview events. The 3D viewport is driven by the same preview
cursor as the timeline.

Run on Windows:

```powershell
make run/win32-debug GAME=examples/combat-action-editor
```

Controls:

- Click **Melee 1H Attack Chop** to select and replay the authored action.
- **Play / Pause** and **Restart** control deterministic action preview; Space
  toggles playback.
- Click empty timeline space to seek.
- Drag an item body to move it; drag a state edge to resize it.
- Edit the selected hitbox start/end in the inspector sliders.
- **Undo / Redo** (or Ctrl+Z / Ctrl+Y) operates on the native timeline
  transaction history.
- Hold the right mouse button over the preview to orbit; use the mouse wheel to
  zoom.

The preview and timeline headers show the current selection, playback state,
edit revision and the last emitted event so every interaction has visible
feedback.

The bundled KayKit Adventurers character, sword and animation libraries are
CC0. See `assets/kaykit/LICENSE-KAYKIT-ADVENTURERS.txt` and
`assets/kaykit/LICENSE-KAYKIT-ANIMATIONS.txt`.
