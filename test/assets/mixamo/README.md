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
| `Idle.fbx` / `Idle.anim.txt` | Idle loop |
| `SlowRun.fbx` / `SlowRun.anim.txt` | Slow run loop (in-place; tests bake planar root motion) |
| `Jumping.fbx` / `Jumping.anim.txt` | Jump |

`.anim.txt` is a compact skeleton+keyframe text export produced by
`tools/anim_import_mixamo/convert_mixamo.cpp` (Assimp → `AnimImporter::exportAnimationFixtureText`).
Unit tests prefer `.anim.txt` so they do not need Assimp at runtime.

## Regenerate `.anim.txt`

```bash
# with Assimp available:
convert_mixamo Idle.fbx Idle.anim.txt Idle
convert_mixamo SlowRun.fbx SlowRun.anim.txt SlowRun
convert_mixamo Jumping.fbx Jumping.anim.txt Jumping
```
