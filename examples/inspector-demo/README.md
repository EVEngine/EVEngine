# Inspector Demo (reflection property panel)

Shows the DevTools-phase property inspector: script classes and their
properties are auto-scanned from the running Squirrel runtime and rendered as
an editable panel (the MVVM architecture from `docs/dev/GUI框架设计.md`).

Run:

```sh
make run/win32-debug GAME=examples/inspector-demo
```

What happens:

- `CharacterData` / `WeaponData` are defined by the game script. Because the
  game is loaded through `dofile()`, `ui.inspect()` calls
  `Runtime::scanClasses()` to reflect every root-table class.
- The panel shows a class combo, an instance combo, and one property row per
  reflected member. Squirrel attribute metadata picks the editor:

  ```squirrel
  </ editor = "slider", min = 0, max = 100 />   hp = 100.0
  </ editor = "combo", options = "warrior,mage,rogue" />  job = "warrior"
  ```

- `ui.inspectObject(hero)` binds the panel to the live `hero` instance.
  Dragging a slider / typing / toggling writes straight back into that object;
  the per-frame `sync()` mirrors external model changes into the panel.
- Inherited members (`WeaponData`) are grouped under a "base" header, so
  parent-class properties are editable side-by-side.

`ui.editorOpen()` mounts the DevTools shell: a menu bar plus three docked
panels —

- **Inspector** (`ui.inspectObject(hero)`): the reflection property panel
  described above. Arrays / tables expand inline (append / remove / edit
  elements), and nested instances open as a sub-panel with a Back button.
- **Database** (`ui.dbRegister(hero, "Hero")` / `ui.dbCreateInstance()`): one
  editable row per registered instance, with the class menu generated from the
  reflected classes.
- **Scene** (`ui.sceneSetPickHandler(...)`): node tree + transform / visibility
  editing through `ISceneQuery`; the Pick button maps a node id back to a
  script instance and opens it in the Inspector.

Hot-reload the script (save `main.nut`) and the panel refreshes the reflected
class shape automatically.
