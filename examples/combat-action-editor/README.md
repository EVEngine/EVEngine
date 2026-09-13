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
  same undo/redo history. Audio, VFX and Prefab blocks expose typed resource,
  playback, lifecycle and shared spatial controls. Audio additionally exposes
  a random clip URI pool, deterministic pitch variation, spatial blend and
  attenuation distances; the advanced payload JSON
  remains available for extensions. Each typed edit merges only its fields,
  preserves unknown payload data, validates the complete result through the
  notify registry, and commits one undo step. Notify type and payload JSON are
  also editable for advanced authoring; registered type, Instant/State shape and
  typed payload contracts are validated before the document changes. **Enabled** persists in schema v4;
  disabled blocks remain visible and editable but do not emit preview/runtime
  events. **New Block** opens a compact registry-backed picker for the target
  track, Instant/State type, duration and payload; valid starter payloads make
  every built-in type immediately insertable at the playhead. Copy/paste uses
  the native deep-copy clipboard and pastes relative to the current playhead.
  Ctrl/Shift-click adds blocks to the selection; the inspector reports the
  complete selection range and aligns starts or ends. Dragging one selected
  body moves the whole selection, while copy and delete operate on every
  selected block in one undoable transaction.
  Dragging also uses an eight-pixel magnetic threshold to snap against the
  playhead, physical splits, animation-section edges and other block edges,
  after applying the deterministic frame grid.
  The **Track** tab selects and renames tracks, toggles mute/lock state, creates
  all eight semantic track kinds, deletes tracks, and deep-copies whole tracks
  across the open document tabs.
  Selecting an animation section reveals its source URI, exact start/end,
  blend-in duration, source trim range and blend curve. **New Section** creates
  validated sections at explicit timeline ranges. Persisted UI state is migrated
  when newer editor controls are introduced, preserving open document state.
  The **Montage** tab edits play rate, looping, Foot IK, animation layer,
  default blend windows and Root Motion masks. Valid changes update both the
  persisted timeline and the already prepared runtime preview. Its Physical
  Sections controls add a split at the live playhead, move an indexed split or
  delete it; every operation preserves strict ordering and participates in the
  same undo/redo history used by section jumps and adaptive time warping.
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
