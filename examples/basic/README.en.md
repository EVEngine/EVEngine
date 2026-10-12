# Basic — First Example

[中文](README.md) · [Example guide](../README.en.md)

A small collection of engine features: Tilemap (`maps/demo.json`), Box2D bodies/gravity, particles and basic 2D drawing. It also demonstrates soft hot reload: one-time initialization in `eve_init`, guards for existing state, and incremental work in `eve_reload` / `eve_asset_reload`.

## Run

With a downloaded SDK, run its bundled copy from the SDK directory:

```sh
bin/eve run share/eve/examples/basic
# Windows:
bin\eve.exe run share\eve\examples\basic
```

With an engine executable on PATH and the repository as your current directory:

```sh
eve run examples/basic
```

For a source build, `make basic` is equivalent to `make run/<platform>-debug GAME=examples/basic` (`win32`, `linux` or `macosx`). Match example sources to the engine/SDK version; dev sources may use newer APIs.

## Try an edit

Open `main.nut`, change a drawing parameter and save. Watch the running example update through the normal reload path. Keep changes in a copy if you want to preserve the original reference game.

The directory contains two UI alternatives. In a copy of the example, replace `main.nut` with the selected script:

- `ui_demo.nut`: multiple declarative UI hosts and dark/light themes.
- `ui_component.nut`: reusable `eve.UIComponent` health bars, sliders and buttons.

For the console, debugger, snapshots and AI panel, continue with [DevLab](../devlab/README.en.md). See the [English user guide](../../docs/en/usr/README.md) for a first game of your own.
