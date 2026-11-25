# Biome Rules Editor example

This example composes EVEngine's UI-neutral editor SDK into a project-specific
biome editor. Native code owns the rules document, spatial domains, PointSet
preview, transactions and undo/redo. The Squirrel presenter draws the four
workspace panels and the exclusion list in the inspector.

Run on Windows:

```powershell
make run/win32-debug GAME=examples/biome-rules-editor
```

Controls:

- Select a layer, then change **Density** / **Priority**. Undo/Redo (Ctrl+Z / Ctrl+Y)
  uses the native document transaction history.
- Layer assets appear in the bottom panel; weight edits go through `biome.property.set.v1`.
- **Add clearing** applies `asset://preview/clearing.spatial` as an exclusion; points
  inside that box disappear. The same seed always rebuilds the same remaining set.
- **Regional schemes** in the left panel selects monsters, animals, plants, surface,
  weather or fog. Enter a resource URI and choose **Apply scheme**, or **Clear scheme**.
  These changes use the existing document undo stack. The bound `preview.south` region
  displays the latest published reference after edits, undo and redo without rebinding.
- Scheme references describe domain-owned resources. This editor does not load those
  resources or simulate weather/monster spawning; the PointSet preview still uses the
  existing biome layer rules. Invalid URIs leave the published configuration unchanged.

The preview domain is a 4x4 box generated in C++ so the example does not need a
baked spatial asset.
