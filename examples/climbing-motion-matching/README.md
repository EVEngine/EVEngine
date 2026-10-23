# Climbing + Motion Matching demo

Run from this directory with a current EVEngine build:

```powershell
..\..\build\win32-debug\src\engine\eve.exe run
```

Controls:

- `W/A/S/D`: move the character; hold `Shift` to run.
- `Space`: ask the climbing runtime to select and execute a vault/mantle.
- `Q/E`: switch between the three obstacle lanes.
- `R`: reset the course.
- `F1`: toggle Motion Matching diagnostics.

The character is a real skinned glTF mannequin. Walking, running and strafing
clips are baked into `MotionDatabase`; `MotionMatcher` evaluates them against
the desired velocity. After each search, `OrientationWarping` copies the matcher
pose into a display pose and yaws the hips/spine toward the remaining planar
error (135° cutoff, 8 1/s smoothing). Search keeps reading the unwarped matcher
pose. Jump/dodge clips are separately bound to climbing action definitions
whose notify contracts are validated before play begins.

The second HUD bar is warp magnitude versus the 135° threshold. Diagonal
movement with a forward clip is the intended visual of orientation warping;
A/D still prefers the baked strafe clips when they score better.

## Why the Epic sample is not included

The official Game Animation Sample contains the requested 500+ Motion Matching
animations and supports exporting Animation Sequences, but its Fab listing also
marks the product as UE-only. Importing those assets into EVEngine would violate
that product restriction. See `assets/ue-only/README.md` for the enforced source
boundary. The example uses CC0 assets so it remains runnable and redistributable.
