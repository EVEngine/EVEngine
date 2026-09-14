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
