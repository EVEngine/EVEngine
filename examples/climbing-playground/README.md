# Climbing Playground

Production-path demonstration for the `climbing` module. It uses real `World3D` bodies,
authoritative candidate probes, deterministic fixed ticks, capsule-constrained motion,
semantic animation-notify validation, and the owning debug snapshot.

Controls:

- `1`, `2`, `3`: select the 0.6 m, 1.0 m, or 1.4 m obstacle.
- `4`: select the descending airborne ledge-grab lane.
- `5`: select the deliberately blocked-clearance lane.
- `6`: select the moving-platform lane.
- `Space`: request the selected action.
- `R`: reset the player marker.

Run on Windows with `make run/win32-debug GAME=examples/climbing-playground`.
