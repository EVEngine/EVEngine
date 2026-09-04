# Avatar Document Editor example

This example composes EVEngine's UI-neutral editor SDK into a project-specific
avatar editor. Native code owns the document, layer composite preview,
transactions and undo/redo. The Squirrel presenter draws workspace panels and
forwards selection and inspector input.

Run on Windows:

```powershell
make run/win32-debug GAME=examples/avatar-document-editor
```

Controls:

- Select a layer in the list or click it in the preview.
- Toggle **Visible** or change **Z** in the inspector; Undo/Redo (Ctrl+Z / Ctrl+Y)
  uses the native document transaction history.
- Drag **Smile** in Parameters to offset the eyes in the composite preview.
- **Expressions** lists channel bindings. Deleting **smile** is rejected while
  `happy` still references it; the previous composite stays on screen.

The seeded face uses authored rectangles only. Live `AvatarInstance` publication
still requires a texture resolver and is not attached in this example.
