# Mixamo locomotion fixtures

Free Mixamo character animations used to exercise `AnimStateMachine` and
`MotionMatcher`.

## Source

Downloaded from the open tutorial repo
[MinaPecheux/UnityTutorials-MixamoAnimations](https://github.com/MinaPecheux/UnityTutorials-MixamoAnimations)
(`Assets/Imports/Idle.fbx`, `Jumping.fbx`, `Slow Run.fbx`).

Original animations are from [Adobe Mixamo](https://www.mixamo.com/) (Y Bot).

## Files

| File | Role |
|------|------|
| `Idle.fbx` / `Idle.eva` | Idle loop |
| `SlowRun.fbx` / `SlowRun.eva` | Slow run loop (in-place; tests bake planar root motion) |
| `Jumping.fbx` / `Jumping.eva` | Jump |

`.eva` is a compact skeleton+keyframe text export produced by
`tools/anim_import_mixamo/convert_mixamo.cpp` (Assimp → `AnimImporter::exportEva`).
Unit tests prefer `.eva` so they do not need Assimp at runtime.

## Regenerate `.eva`

```bash
# with Assimp available:
convert_mixamo Idle.fbx Idle.eva Idle
convert_mixamo SlowRun.fbx SlowRun.eva SlowRun
convert_mixamo Jumping.fbx Jumping.eva Jumping
```
