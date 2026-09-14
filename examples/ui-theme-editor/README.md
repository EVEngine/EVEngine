# UI Theme Editor example

This example composes EVEngine's UI-neutral editor SDK into a named Theme
workbench. Native code owns the catalog, token transactions and undo/redo.
The Squirrel presenter draws Themes / Preview / Inspector panels. The gallery
host applies a host-local Theme override so live controls show the selected
scheme without mutating `ui.getTheme()` until **Activate**.

Run on Windows:

```powershell
make run/win32-debug GAME=examples/ui-theme-editor
```

Controls:

- Select a scheme in the Themes list. **New from Dark / Light** and **Duplicate**
  create named assets; **Delete** refuses to remove the last scheme.
- Inspector edits `color.button` and `color.windowBg` with color palettes, plus
  rounding and font scale, through catalog transactions. Undo/Redo (Ctrl+Z /
  Ctrl+Y) uses native history.
- **Activate** publishes the selected scheme to `ui.setGlobalTheme` (`dark`,
  `light`, or `custom`). The center gallery stays on the selected preview copy.
