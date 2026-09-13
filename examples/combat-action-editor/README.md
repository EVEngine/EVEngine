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
  `Content/Actions`, validates it, and registers its stable GUID sidecar with
  AssetDB. **Open Asset** provides a searchable picker over indexed, validated
  Montage documents; selecting an already-open GUID activates its existing tab.
- The compact transport icons control deterministic action preview; Space
  toggles playback.
- Click empty timeline space to seek.
- Drag an item body to move it; drag a state edge to resize it.
- Selecting an animation section, instant notify, or state window automatically
  opens the **Action Block** inspector. Its timing sliders commit validated,
  undoable transactions; the toolbar deletes the selected block and exposes the
  same undo/redo history. Notify type and payload JSON are editable for advanced
  authoring; registered type, Instant/State shape and typed payload contracts are
  validated before the document changes. **Enabled** persists in schema v4;
  disabled blocks remain visible and editable but do not emit preview/runtime
  events. **New Block** opens a compact registry-backed picker for the target
  track, Instant/State type, duration and payload; valid starter payloads make
  every built-in type immediately insertable at the playhead. Copy/paste uses
  the native deep-copy clipboard and pastes relative to the current playhead.
  The **Joint** tab retains skeleton
  transform and key editing in the same compact side panel.
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
