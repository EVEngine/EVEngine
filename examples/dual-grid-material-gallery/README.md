# Dual Grid Material Gallery

Seven offline material previews moved out of `map.dualGridMask.*Preview` tests.
Keys `1`-`7` show synthetic transitions, source-art transitions, two-material
map, three-material map, world-space water splat, sand/stone splat and
four-material splat. Previews use the original algorithms and parameters.

```sh
make run/win32-debug GAME=examples/dual-grid-material-gallery
```

The native baker uses C++ APIs not exposed to scripts. Rebuild from a configured
host engine tree with `cmake --build build/win32-debug --target dual_grid_material_bake`.
Run that executable with this example directory as its working directory to
regenerate all `previews/*.png`. No test runner or environment variables are needed.
Source art and its notice are in `assets/`; preview files are baked offline,
not screenshots of engine rendering. The normal example displays them as textures.
