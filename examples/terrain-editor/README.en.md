# Terrain Editor

[中文](README.md) · [Example guide](../README.en.md)

This example builds an editor inside the running game using `editor` and `ui`. The viewport renders a 3D heightfield while the tool panel selects raise/lower brushes that update its grid in place.

The heightfield is encoded as a versioned `eve.terrain-editor-document/1` document and saved atomically in the application data directory. On the next launch, the engine validates and restores it. The same C++ codec used by the level editor checks format versions, unknown fields, dimensions and non-finite values.

## Run

From the repository root, with a compatible runtime on your path:

```sh
eve run examples/terrain-editor
# Alternatively, build and run from source:
make run/<platform>-debug GAME=examples/terrain-editor
```

See the [example guide](../README.en.md) for SDK/source version matching.

## Controls

| Input | Action |
| --- | --- |
| Right-drag the viewport | Orbit the camera |
| Mouse wheel | Zoom |
| Hold the left mouse button in the scene | Raise or lower terrain |
| Raise / Lower | Select the brush |
| Regenerate | Generate a new heightfield |
| Save terrain | Atomically save the versioned terrain document |
| Reload saved terrain | Validate and load the saved document |

The same UI can stay in a shipped game so players can edit content at runtime.

## Scope

The interactive viewport uses a 64×64 heightfield. Larger archive round trips and 1K/4K/8K random-access performance belong to the `procgen.terrain.performance` automated tests; the GUI does not allocate an 8K heightfield on every launch. This guide does not claim a new benchmark run.
