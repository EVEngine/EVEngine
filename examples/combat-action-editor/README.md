# Combat Action Editor example

This example composes EVEngine's UI-neutral editor SDK into a project-specific
combat action editor. It uses the canonical `eve.action.timeline` asset model,
native hit testing and drag preview, one-step transactions, undo/redo, and
deterministic preview events. Its document toolbar opens two independently
persisted montage assets, projects dirty state into tabs, and protects unsaved
work when a tab is closed. The 3D viewport is driven by the same preview cursor
as the active timeline.

Run on Windows:

```powershell
make run/win32-debug GAME=examples/combat-action-editor
```

Controls:

- Switch between **Light Attack** and **Follow-up** document tabs. An asterisk
  marks unsaved edits; closing a dirty tab requires a second explicit click to
  discard its changes.
- The save icon or Ctrl+S atomically writes the active document under
  `Content/Actions`; reopening the resource restores the saved timeline.
- The compact transport icons control deterministic action preview; Space
  toggles playback.
- Click empty timeline space to seek.
- Drag an item body to move it; drag a state edge to resize it.
- Edit the selected hitbox start/end in the inspector sliders.
- The Presentation lane includes a cubic master-volume parameter curve. Its
  sampled shape is drawn inside the block; native inspector/script APIs edit
  its keys through the same undoable timeline transaction.
- The impact camera cue drives the preview's real `CameraController` with a
  deterministic position, rotation and FOV impulse.
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
