# Particle Playback Lab

Visual validation scene for deterministic playback, a versioned multi-emitter
effect asset, looping bursts, bounded fixed stepping, and distance-based
emission. It intentionally uses untextured particles so it has no downloaded
asset dependency.

```powershell
make run/win32-debug GAME=examples/particle-playback-lab
```

The cyan/purple trail is emitted at even world-distance intervals along a
Lissajous path. `impact.effect.json` composes the orange center core and sparks
under one transform and exposed `intensity` parameter.
