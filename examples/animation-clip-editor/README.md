# Animation Clip Editor example

This example composes EVEngine's UI-neutral editor SDK into a project-specific
animation clip editor. Native code owns the clip document, pose scrub preview,
skeleton overlay, dope-sheet layout and undo/redo. The Squirrel presenter draws
the four workspace panels and forwards pointer/transport input.

Run on Windows:

```powershell
make run/win32-debug GAME=examples/animation-clip-editor
```

Controls:

- Click a bone name in the Skeleton list or the dope-sheet label column to select a track.
- Click the timeline to seek; Play / Pause / Stop (Space) advance the pose preview.
- Change **Duration**, **Loop**, or **Mask** in the inspector; Undo/Redo (Ctrl+Z / Ctrl+Y)
  uses the native clip transaction history.
- The center viewport draws `joint` / `bone-line` overlay primitives from
  `AnimationClipPreview`. No KayKit mesh is required: the editor seeds a two-bone walk.
