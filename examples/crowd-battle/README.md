# Crowd battle

A complete native RTS battle rendered from authoritative unit poses and health. Two armies use coordinated AttackMove, shared navigation, predictive avoidance, body contact, real weapon fire, damage, death and member cleanup. No script changes health or replaces the combat pipeline.

Run `make run/<platform>-debug GAME=examples/crowd-battle`. Space pauses/resumes. Blue/red squares identify factions; small health bars show actual damage; yellow centres show traffic or melee admission waits. The top bars show surviving army sizes.

Set `battleMode` in `config.nut`, then restart: `open` for formation combat, `corridor` for a one-cell opening and reform, or `melee` for short-range front/rear admission. The first supported melee slot target is another Unit. `battleUnits` supports even counts from 16 to 512 in this compact visual arena; the native benchmark covers 1000/5000/10000 without rendering.

The example advances one injected 0.05-second simulation tick per render update. It intentionally does not accumulate a wall-clock backlog. A slow build runs the battle more slowly; this example is visual functional evidence, not an FPS benchmark. Graphics use simple rectangles; no asset download is required.
