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

## 2026-11-12 画面改善

增加随权威 feet/phase 移动的分件角色、训练道标识、可读 HUD 和近景/总览切换；修正复位点超出探测范围、旧错误字段和移动平台升高后低位射线落空的场景配置。原模块净空、胶囊和拒绝检查保留。

[当前实帧](../../docs/images/examples/review-2026-11-12/nine-improved/climbing-playground.png) · [前后与状态对照](../../docs/images/examples/review-2026-11-12/nine-improved/README.md)
