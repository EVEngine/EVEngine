# Package-backed hologram example

Run from the repository root with Vulkan SDK tools on PATH. Enable the optional
`asset_stylize` module in your normal engine configuration. On Windows, the
verified trimmed configuration was `minimal` plus `asset_stylize`, `font` and
`devtools` (the latter two satisfy existing graphics/CLI link dependencies).
Use the normal README dependency setup before configuring a fresh build.

```powershell
cmake/with-msvc.cmd cmake.exe -S . -B build/win32-debug -DEVENGINE_PROFILE=minimal -DEVENGINE_MODULE_asset_stylize=ON -DEVENGINE_MODULE_font=ON -DEVENGINE_MODULE_devtools=ON
cmake/with-msvc.cmd cmake.exe --build build/win32-debug --target shader_effect_probe -j 4
New-Item -ItemType Directory -Force build/shader-demo | Out-Null
glslc --target-env=vulkan1.2 examples/shader_effect_package/effect.vert -o build/shader-demo/effect.vert.spv
glslc --target-env=vulkan1.2 examples/shader_effect_package/effect.frag -o build/shader-demo/effect.frag.spv
glslc --target-env=vulkan1.2 test/asset_import/UnityPreviewDisplay.frag -o build/shader-demo/display.frag.spv
python scripts/package_shader.py --vertex build/shader-demo/effect.vert.spv --fragment build/shader-demo/effect.frag.spv --interface mesh3d --parameters examples/shader_effect_package/parameters.json --output build/shader-demo/effect.json
$env:EVENGINE_VULKAN_VALIDATION='1'
build/win32-debug/test/shader_effect_probe.exe build/shader-demo/effect.json build/shader-demo/output build/shader-demo/display.frag.spv
```

The executable writes `author/effect.eva`, `consumer/effect.evpack`, and five
engine-owned PNG captures. It exits nonzero if a behavioral check fails. The
left sphere animates; the right retains its own weaker intensity and tint.
`failed-reload.png` and `reloaded.png` must equal `initial.png` byte-for-byte.
`animated.png` differs only on the left; `stopped.png` removes the left effect.

Author parameters in declaration order: `phase` is slot 0, `strength` slot 1.
The probe authors a MeshVfx curve from phase 0 to 2 over a two-second cycle.
To use this in a game, prepare a source candidate with `prepareMeshVfxPackage`,
publish with the existing asset store and Cook pipeline, then load the cooked
entrypoint with `EvpackMeshVfx::load`. Keep Graphics alive longer than the
instance; call `advance(dt)` and `draw` inside your existing render lifecycle.
The example selects additive transparent blending without depth writes.

For a standalone shader source asset, the CLI also supports:

```powershell
eve asset import build/shader-demo/effect.json --from shader --package-id 018f6f22-2490-7ad2-bf58-4f1dbca31040 --name example.effect --out effect.eva
eve asset cook effect.eva --target windows-x86_64-vulkan --out effect.evpack
```
