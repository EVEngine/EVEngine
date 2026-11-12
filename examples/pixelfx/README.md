# Procedural PixelFX Act workbench

A native, procedural gallery of all244 compiled effects from patch-gg/pixelfx
revision `a84159688af3ca37a4be05a0d422b94451b2e24e` (MIT). No JS runtime,
baked effect sheets or upstream gallery fonts are used. The existing licensed
DejaVu font and caller-authored procedural character pixels are retained.

Use the engine built from this branch; older SDKs do not expose its Act classes:

```powershell
make build/win32-debug JOBS=4
make run/win32-debug GAME=examples/pixelfx
```

The left preview is the default configuration; the right retains the authored
parameter examples in `variants.nut` and accepts live edits. Search and ordering
come from the sole compiled catalogue. Only the selected pair creates effect
programs/Acts. Two fixed640x400 surfaces, four textures and four sprites are reused
across selection; the native frame builder computes only each logical view
rectangle and clears output padding. Default source bounds fit these surfaces.
Arbitrary edited geometry may extend outside this gallery viewport; applications
can allocate their own larger canvases through the same native API.

## Controls

- Tab or `/`: edit ID filter. Up/Down select matching IDs; Enter/Escape leave search.
- Space: pause/play. R: restart both current configurations. S: source stop/fade.
- `,` / `.`: previous/next simulation frame. PageUp/PageDown:30 frames.
- 1/2/3/4: quarter/half/normal/double playback speed.
- B: automatic body, source rig, input sprite, or effects-only presentation.
- T: target visibility. F: mirror. V: composite/color/light view. G: backdrop.
- Q/E: rotate the complete preview by15degrees. Z: return the view to zero.
- `[` / `]`: select a parameter on the right. `+` / `-`: adjust it.
- Boolean parameters toggle; ramps rotate; individual colors adjust a channel.
  Actor parameters cycle supported stock/custom/none inputs and validate normally.
- Point parameters use P to switch x/y. Text parameters use Enter to edit,
  Backspace to erase, Enter to apply or Escape to cancel. Text entry is keyboard ASCII;
  the native configuration API separately supports validated UTF-8 values.

Invalid edits keep the old preview and show the diagnostic. Successful edits
publish a new right-hand Act; the left simulation is retained. The title strip
shows the chosen presentation/view modes. The time bands show authored windup,
hit markers and the current Act phase; live hitstop and advisory completion are
shown below. Camera offsets follow the native shake observation.

## Playback and ownership

The gallery explicitly feeds a60Hz update sequence to native ActRuntime, retaining
fractional accumulated time. Global speed and configured playbackSpeed apply once;
effect options named speed retain their separate physical meaning. One-shot
preview repetition waits48 additional updates after advisory completion; native
rendering is not disabled by done. Loops keep native RNG progression.

Backward frame navigation uses the native prepareReplay builder and its single-use
publication token to replay the60Hz simulation
before replacing the visible owner; recorded stop timing is reapplied. This UI
replay is bounded to7200 updates/120 seconds. It does not promise rollback of the
shared source pose-cache history: source fidelity still depends on the documented
bounds/frame call order. R starts a fresh simulation in the shared render session.
Replay retains the original immutable Act recipe and accepts explicit step/stop
history; it does not repeat external callback side effects.

Source-rig presentation uses source authored pose/target orchestration. Input
sprites use actual copied RGBA independently of the effect mask, with translation
and upper-row lean/crouch shear; arbitrary images do not acquire inferred limbs.
Effects-only omits the external stand-in; effect-owned actor pixels can still be
present, as with source body:false.

Whole-preview rotation transforms the final color/light sprites together around
their common center, including body, targets, ghosts and dust. Both planes use
the same angle, fitted scale and rotated shake offset. Backdrops remain fixed.
This presentation transform does not change native effect-local angle semantics,
advance simulation, recompose CPU planes or allocate replacement textures.

## Validation boundary

The native keyboard-driven gallery has been traversed through244 unique IDs and
wraparound, with engine-owned frame captures for windup/hitstop, custom carrying,
mirroring, light buffers, parameters and search. The script bridge separately
passes real-VM ownership and all244-effect workflows. Before the final dev refresh,
both Windows linkage routes passed917 focused cases; custom/max-size actors,
whole-preview rotation and three full catalogue cycles were exercised. CPU,
allocation, particle-container and texture-update observations are retained locally.
Platform math can still change individual raster/occlusion-edge pixels; universal
bitwise equality is not claimed. Final PR-head/platform CI remains authoritative.

See the [port plan](../../docs/dev/plans/2026-11-03-pixelfx-port.md),
[native/script API](../../docs/usr/modules/stylize-pixelfx.md) and source-owned
notices under `src/modules/stylize/pixelfx/`. Historical raw reports, measurements
and engine screenshots stay local under ignored `build/pixelfx-validation`.

Earlier per-effect parameter walkthroughs and milestone evidence are preserved in
[LEGACY_NOTES.md](LEGACY_NOTES.md); its former page keys are historical.
