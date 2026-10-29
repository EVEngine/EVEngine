# Toy WebGPU runtime validation

Date: 2026-10-30. The independent EVEngine-Toy exporter calls the existing
`eve zip` command, mounts its archive as `/game/game.eve`, and builds the real
Emscripten host with its external Toy plugin. No replacement game runtime is used.

## Engine fixes required by the real browser run

- Enable `-fexceptions` during engine and dependency C++ compilation and host
  linking. A minimal Emscripten program reproduces an uncaught exception with
  link-only settings (exit 1), while compile-and-link settings reach the catch
  block (exit 0). The original browser startup stopped at structural metadata
  analysis because its recoverable budget exception escaped into JavaScript.
- Configure browser canvas textures with `COPY_SRC` as supported by the WebGPU
  canvas API. The C API adapter synthesizes surface capabilities without filling
  `usages`; that field is not a browser surface-usage query. A direct browser
  probe configured the canvas, rendered a color, and copied `[191,128,64,255]`
  from BGRA8 with no validation error. Native surfaces retain their capability
  query.
- Reject screen readback before a presented frame exists and propagate failed
  texture copies instead of encoding an empty image. Unsupported native readback
  also no longer interrupts presentation. Temporary image ownership is released
  only after successful copying.

Sources: [Emscripten exceptions](https://emscripten.org/docs/porting/exceptions.html)
and [GPUCanvasContext configuration](https://developer.mozilla.org/en-US/docs/Web/API/GPUCanvasContext/configure).

## Actual result

The exporter selected the existing `minimal` profile with `font` explicitly
enabled, using Emscripten 6.0.7, CMake 3.29.6, and single-thread system libraries.
The test game runs its EveScript update/render loop, calls the official Toy SDK
through the plugin, and responds to arrow-key input.

Headless Edge loaded the package under `/toy/local/` without isolation headers:
`crossOriginIsolated=false`, `SharedArrayBuffer` absent, WebGPU device creation
successful. The engine saved an 800x600 PNG containing 1024 blue player pixels
and 1600 red obstacle pixels. Keyboard movement reached the script update loop.
No JavaScript runtime error or frame submission error was reported.

`make check` passed against `origin/dev`, with clang-format 18.1.8. Plugin
contracts passed 21 tests; ZIP integrity and entry structure were checked.
The independent project's `test/browser_smoke.py` retains the browser checks,
including color assertions that reject an all-black image.

The Toy-hosted preview, Bilibili App WebView, authenticated cloud storage and
leaderboards, and account-specific upload quota were not tested. This is local
browser evidence, not a published Toy or a native Dawn test-suite result.

## Architecture scope

No public signature, ECS type, persistent game format, or engine module boundary
changed. The fixes preserve owning image returns, exception-to-failure handling,
and provider-dependent surface behavior. No dependency implementation or copied
header override was added. No architecture-rule exception was taken.
