# Climbing + Motion Matching demo

Run from this directory with a current EVEngine build:

```powershell
..\..\build\win32-debug\src\engine\eve.exe run
```

Controls:

- The demo starts at the wide-wall contact sample. `F2` cycles through wide wall,
  free hang, narrow one-foot support, ladder, climb-up, outer/inner corner and backward jump; `Space` traverses to the
  next authored anchor or climbs onto the platform where available.
- `R` leaves the contact samples and resets the parkour course.
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

Before skinning, the example copies the evaluated animation pose and applies
independent hand and foot TwoBoneIK constraints. Foot targets require a valid
physics contact or authored ladder socket. The four HUD bars show left hand,
right hand, left foot and right foot weights, in that order. Presentation weights
blend over 0.12 seconds using the fixed simulation delta; reset clears them.
This presentation state never drives the capsule or action selection.

`assets/contact-*.json` are EVEngine-authored versioned profiles and anchor graphs.
The KayKit rig uses a 0.2 m capsule radius with no extra hang offset, so its short
limbs can reach the wall. The contact samples check active bone endpoints within
0.05 m of their targets and print per-foot physics validity when the phase changes.
Set `contactLabAutoAdvance = true` in `config.nut` for a deterministic 40-second
cycle through all eight samples, including horizontal transfer, ladder ascent and
climb-up. The climb-up sample checks its final capsule position against the platform
landing before accepting completion.

## Why the Epic sample is not included

The official Game Animation Sample contains the requested 500+ Motion Matching
animations and supports exporting Animation Sequences, but its Fab listing also
marks the product as UE-only. Importing those assets into EVEngine would violate
that product restriction. See `assets/ue-only/README.md` for the enforced source
boundary. The example uses CC0 assets so it remains runnable and redistributable.

Corner samples use real perpendicular collision walls. The runtime routes the capsule
and hands through the intersection of the two support planes; presentation yaw follows
the authored turn. Backward jump crosses to the opposite wall, releases limb constraints
during flight, and reattaches near arrival. These samples reuse the CC0 base animation
with contact IK; they are not a replacement for dedicated authored climbing clips.
