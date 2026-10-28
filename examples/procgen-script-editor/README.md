# Procgen Script Editor example

Hosts an encapsulated Squirrel generator (`generators/forest.nut`). Native code
owns the parameter document, undo, and PointSet preview copies. The presenter
runs `generate` after parameter commits and publishes the result.

```powershell
make run/win32-debug GAME=examples/procgen-script-editor
```

- Inspector sliders commit into the native Params document, then rebuild.
- **Live** still uses commit-on-change; keep it off unless you want every slider
  event to rebuild immediately as the UI reports it.
- Debug stages list `candidates` / `trees`.
- Ctrl+Z / Ctrl+Y undo parameter edits; a failed generate keeps the last points.
- Saving `generators/forest.nut` reloads the schema (unknown keys dropped,
  missing keys filled from defaults).
