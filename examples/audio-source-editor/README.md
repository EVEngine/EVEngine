# Audio Source Editor example

This example composes EVEngine's UI-neutral editor SDK into a project-specific
audio source editor. Native code owns the source document, waveform envelopes,
audition transport, transactions and undo/redo. The Squirrel presenter draws
the four workspace panels and forwards pointer/transport input.

Run on Windows:

```powershell
make run/win32-debug GAME=examples/audio-source-editor
```

Controls:

- **Play / Pause** and **Stop** drive revision-safe audition. Space toggles playback.
- Click the waveform to seek. Loop markers follow Inspector loop start/end.
- Change **Volume**, **Pitch**, or loop bounds in the inspector; Undo/Redo
  (Ctrl+Z / Ctrl+Y) uses the native source transaction history.
- Live OpenAL audition is attached when the `audio` module is available; otherwise
  the playhead still advances on the clock backend so the layout stays testable.

The preview tone is generated in C++ (`asset://preview/tone.sine`) so the example
does not need a bundled clip.

The shared `EditorWorkspacePresenter` mounts the workspace and generates Inspector fields from the native presentation schema. It routes history, shortcuts and diagnostics through the domain editor. Numeric drags commit once on release; Escape, focus loss and close cancel the draft. See [framework migration](../../docs/dev/2026-10-23-editor-ui-migration.md).

Audio output is disabled by default: the example uses the silent clock transport and starts stopped. Play/Pause advances only the visual playhead.

预览在后台生成，由 `update()` 发布；等待期间保留上一份有效结果。Inspector 显示预览状态，关闭共享 presenter 会取消待处理结果。provider 重绑及 generation 契约见 `docs/dev/2026-10-24-async-editor-preview.md`。
