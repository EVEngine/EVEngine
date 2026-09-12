# Animation Clip Editor example

This example composes EVEngine's UI-neutral editor SDK into a project-specific
animation clip editor. Native code owns the clip document, joint hierarchy, pose scrub preview,
skeleton overlay, transform keys, dope-sheet layout and undo/redo. The Squirrel presenter draws
the four workspace panels and forwards pointer/transport input.

Run on Windows:

```powershell
make run/win32-debug GAME=examples/animation-clip-editor
```

Controls:

- Click a bone name in the Skeleton list or the dope-sheet label column to select a track.
- Click the timeline to seek; Play / Pause / Stop (Space) advance the pose preview.
- Use the Position, Rotation and Scale controls to edit the selected joint at the playhead.
  Editing automatically creates a stable transform key when the frame has none.
- Use **Set Key**, **Delete Key**, and **Key time** to insert, remove or retime a key. All edits,
  including joint properties, use the native clip transaction history and Undo/Redo.
- Change **Duration**, **Sample rate**, **Loop**, or **Mask** in the clip section.
- The center viewport draws `joint` / `bone-line` overlay primitives from
  `AnimationClipPreview`. No KayKit mesh is required: the example seeds a six-joint hierarchy.
