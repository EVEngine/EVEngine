# Basic — 2D, one step at a time

[中文](README.md) · [Example guide](../README.en.md)

Start with one rectangle, then learn images, movement and physics before adding particles and fire.

## Run and select a lesson

```sh
eve run examples/basic
# Source build: make basic
```

From an extracted SDK, run `bin/eve run share/eve/examples/basic` (Windows: `bin\eve.exe`).
Edit `tutorialStep = 1` in that copy's `config.nut`, choose a number below, then **close the window and restart**.
The module list is selected at startup. Within a lesson, script edits support hot reload.
`main.nut` selects one complete script from `steps/`; no file copying is needed.

## Lessons

1. **Rectangle** — [01-rectangle.nut](steps/01-rectangle.nut): initialization, rendering, clearing and pixel coordinates. Change its position, size and color.
2. **Image** — [02-image.nut](steps/02-image.nut): replace the rectangle with bundled `assets/box.png`. Load once during initialization; draw each frame.
3. **Movement** — [03-movement.nut](steps/03-movement.nut): hold arrow keys to move within the window. Multiply pixels per second by `dt` in seconds.
4. **Physics** — [04-physics.nut](steps/04-physics.nut): replace keyboard movement with gravity and a floor. Physics owns position; rendering reads it. Bodies use center coordinates, images use top-left coordinates.
5. **Particles** — [05-particles.nut](steps/05-particles.nut): add sparks following the box. Update physics, position the emitter, then update and render particles.
6. **Fire** — [06-fire.nut](steps/06-fire.nut): keep the sparks and add fire. Edit colors, speed, lifetime and emission rate in [particles/fire.json](particles/fire.json); the asset reloads automatically.

Lessons 1–2 only draw; lesson 3 adds keyboard input; lesson 4 adds physics; lessons 5–6 add particles.
`persist` plus null guards preserve textures, worlds and emitters across script reloads.
Reloading keeps the box position; restart to watch it fall again.
The original 16×16 RGBA checker box is included with this example and needs no external assets.

## Next

`ui_demo.nut` and `ui_component.nut` are separate advanced UI alternatives. In a copy, replace `main.nut`
with one of them and remove the `modules` field from `config.nut` so their required modules are instantiated.
Continue with [Game Tutorial](../game-tutorial/README.md) for a small game or [DevLab](../devlab/README.en.md)
for the debugger, console and AI panel. Match example sources to your engine/SDK version.
