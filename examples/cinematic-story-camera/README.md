# Cinematic Story Camera

Story-scene cinema on top of gameplay follow: press Space to push a full-scene
overview, then the director plays dolly → crane → orbit-reveal moves and pops
back to the player camera.

Uses `eve.CinemaDirector` (bind → `defineOverview` → `pushOverview` → film moves
→ `pop`). Do not rebuild this with raw `addView` / `activateRig` recipes.

```
make run/win32-debug GAME=examples/cinematic-story-camera
```

## Controls

- **Space** — start the story beat (push plaza overview)
- **Esc** — force-pop back to gameplay if a beat is active
- Auto — after the overview lands, dolly in, crane up, orbit reveal, then restore

## Contract

```squirrel
local dir = eve.CinemaDirector();
eve.result.ignore(dir.bind(ctrl), "bind");
eve.result.ignore(dir.defineOverview("plaza", 18, 14, 18, 0, 1, 0, 42), "define");
eve.result.ignore(dir.pushOverview("plaza", 0.8), "push");
eve.result.ignore(dir.dolly(4.0, 1.4), "dolly");  // positive = toward subject
eve.result.ignore(dir.pop(0.7), "pop");
// eve_update: dir.update(dt) then ctrl.update(dt)
```
