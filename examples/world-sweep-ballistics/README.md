# World3D projectile sweep laboratory

Run `eve run examples/world-sweep-ballistics` with `weapon`, `physics`, `font`, and graphics enabled.
The four panels render owning projectile states and the actual World3D box geometry in an orthographic XY view.
They demonstrate continuous ricochet, notification-only contacts, a user-written velocity function with
persistent pierce memory on the return trip, and disabled collision against the same physical obstacles.

Every pool binds `world.queryToken()` with sphere radius 0.12 and explicit category/mask/ignored-body filters.
Physics and projectiles step at 120 Hz; catch-up retains accumulated elapsed time. The marker
`WORLD_SWEEP_BALLISTICS_READY` requires actual contacts in all three enabled panels and no disabled contacts.
The sphere drawn at each projectile has the same radius as its sweep. Amber marks the center at impact and normal.

World ownership stays in the example; pools retain query sessions with handles only. Hot reload is disabled:
recreate/rebind an empty pool after restoring or replacing a world. Queries do not apply physical forces.
Unmapped physics contacts have body/shape provenance and no fabricated ECS damage target.

Font: bundled DejaVu Sans Bold, accompanying license in assets.
