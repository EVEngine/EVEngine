# Explicit projectile / physics composition

Run `eve run examples/projectile-physics-composition`.
The left amber panel only queries a dynamic box and emits a notification; its box stays still.
The right green panel explicitly consumes the committed notification with
`world.applySweepContactImpulse(event, ix, iy, iz, px, py, pz)` and pushes its box at 2 m/s.
Both projectiles are released on impact, so each event is consumed exactly once.

This is an opt-in composition example, not a default engine coupling. The game chooses
the impulse, application point, update order and which event kinds to consume. Both worlds
step at 120 Hz before their projectile pools. The impulse affects the following physics step.
`PROJECTILE_PHYSICS_COMPOSITION_READY` verifies real contacts and both velocity outcomes.

No ECS target or damage is inferred. The point is explicitly the box center, not the sphere-center
contact position. Rebuild bindings after world restore; stale contact commands fail.
This does not provide relative-motion CCD for fast moving targets or an atomic transaction
spanning the carrier frame and physics feedback. Handle each returned Result.
