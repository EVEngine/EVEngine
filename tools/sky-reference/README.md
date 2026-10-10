# Sky reference tooling

These are offline calibration/import tools for the independent sky system. They
do not implement the runtime sky and a passing reference repeatability report
does not establish EVEngine/Unreal visual parity.

## Requirements

Use Python 3.10+ with NumPy and Pillow for capture validation and image comparison.
The DDS reader and source export host use only the standard library. Source export
and reference capture require an installed Unreal Editor and a **disposable local
project** containing the licensed source assets under `/Game/UltraDynamicSky`.
Enable PythonScriptPlugin and EditorScriptingUtilities in that project. The tools
create reference maps and project-local caches. They never modify the original
vendor package. Keep source exports, screenshots, caches and copied vendor assets
outside version control (for example under `.local-debug/`).

```powershell
python tools/sky-reference/export.py --project <project.uproject> --editor <UnrealEditor-Cmd.exe> --output .local-debug/uds-assets
python tools/sky-reference/capture.py --project <project.uproject> --editor <UnrealEditor-Cmd.exe> --output .local-debug/uds-calibration
python tools/sky-reference/compare.py <reference.png> <candidate.png> --output .local-debug/sky-comparison
```

Output paths must not exist. Jobs write to unique sibling staging directories;
only fully validated packages are renamed into the requested output. Failed jobs
retain their logs and intermediate artifacts. Do not run multiple editors against
the same disposable project concurrently.

## Source package contract

`eve.sky-source-package/1` contains the source engine version, original content
mount, inventory, exported artifact hashes and normalized texture metadata.
Artifacts include DDS textures and T3D material/function/curve/blueprint exports.
The T3D files are source evidence, not executable EVEngine materials.

The reader supports uncompressed DX10 R8, R16 UNORM, RGBA8, BGRA8, RGBA16 UNORM,
RGBA16F and RGBA32F. BGRA is reordered without quantization. HDR data remains
IEEE-754 binary16/binary32, including values outside [0,1]. The source asset's
explicit sRGB metadata controls interpretation. Mip offsets, depth, array/cube
layers, sampler settings and byte lengths are recorded; compressed, malformed,
oversized and unsupported formats fail instead of silently changing precision.

`eve.sky-source-export-request/1` rejects unknown fields and versions. Package
version 1 has no earlier format to migrate. Consumers must explicitly reject
unknown versions rather than reinterpret them as version 1. Original material
and curve parameters must be translated explicitly before runtime use.

## Static capture contract

`scenarios.json` uses `eve.sky-reference-capture/1`. All fields are required;
unknown fields/versions, nonfinite values, duplicate names and path traversal are
rejected. Coordinates are Unreal centimeters; rotation is `[pitch,yaw,roll]` in
degrees; FOV is horizontal. Time and cloud coverage retain UDS source units
(`1200` means noon; coverage ranges from 0 to 10).

Each scenario starts a fresh editor process, creates a sky and camera, disables
time/cloud motion, star twinkle speed and random starting state, fixes manual exposure, warms the
renderer, then captures two images. `r.Test.FreezeTemporalSequences=1` and disabled
grain quantization fix the calibration sampling sequence. Every scenario must
repeat with **100% exact RGB pixels** and SSIM >= 0.999999 before publication.
The manifest records requested settings, fixed console variables, source engine
version and SHA-256 hashes; an actor T3D snapshot preserves resolved components.
The set manifest is `eve.sky-reference-set/2`; each scene manifest is
`eve.sky-reference-result/2`. Input `rotationDegrees` is explicitly ordered
`[pitch, yaw, roll]`. The worker uses named Unreal Rotator arguments and records
the actual camera position, Euler angles, forward/up vectors and horizontal FOV.
Publication independently checks those vectors against the requested orientation.
Version-1 images predate this read-back check: the positional Unreal constructor
interpreted the first value as roll. They must not be used for cross-engine
comparisons against a pitched camera, even if their repeated captures match.

This is a static calibration suite. It does not test temporal convergence, cloud
motion, transitions, camera movement, weather particles, aurora or space mode.
Those need separate dynamic scenarios with synchronized time and RNG. Default
UDS sky/color/quality modes remain visible in the actor snapshot; changing those
defaults produces a different baseline and must not be mixed into an old set.

## Native atmospheric configuration

The native configuration `examples/uds-sky/sky.json` uses
`eve.sky-profile` version 2. Version 1 migrates with height fog disabled. Load JSON with common `Value::fromJson`, validate
with `daynight::SkyProfile::decode`, then prepare `SkyRuntime` using its settings
and atmosphere accessors. Preparation is separate from contributor attachment.
This file describes the current atmosphere and background-fog stage only: it does not claim to
reproduce the complete clear-noon UDS image, including wispy clouds and the reference display transform.
Unsupported modes, versions and unknown fields are rejected explicitly.

## Pixel comparison contract

`eve.sky-image-comparison/1` compares registered, same-size, 8-bit SDR RGB output
in the same declared color space. It does not resize, align, fit exposure or
compare alpha. The default proposed parity gate requires at least 99% of pixels
to have **every RGB channel within 3/255**, and RGB-mean SSIM >= 0.99. SSIM alone
is not a pixel similarity percentage. SSIM uses an 11x11 Gaussian window with
sigma 1.5, valid convolution, population covariance, and range 255.

`--regions regions.json` accepts named `[x,y,width,height]` regions (minimum 11x11).
The full frame and **every** supplied region must pass independently. Reports
include exact pixel fraction, threshold fraction, MAE, maximum error, 99th
percentile error, PSNR and SSIM. PSNR is JSON null for identical images. Error
images use actual maximum channel errors without amplification; the binary mask
shows all pixels outside tolerance. Exit codes: 0 pass, 1 mismatch, 2 input error.

```powershell
python -m unittest scripts.tests.test_sky_reference_dds -v
python -m unittest discover -s tools/sky-reference/tests -v
```

## Sky dome corner data

Export `Ultra_Dynamic_Sky_Sphere` with Unreal's `StaticMeshExporterFBX` and
`FbxExportOption.ascii=True`, then decode its thin-cloud attributes:

```powershell
python tools/sky-reference/sky_mesh.py sky-sphere.fbx sky-sphere.mesh.json
```

The decoder supports one triangulated mesh with indexed polygon-corner UV/color
layers, and rejects other layouts instead of inventing attributes. It reverses
Unreal's FBX Y-axis and texture-V conversion. `eve.sky-mesh/1` stores a triangle
list of `[x,y,z,u,v,r,g,b,a]` corners in **Unreal local centimetres**, UV channel 3
by default, with linear vertex colors and the input SHA-256. Repeated positions
retain their distinct UVs/colors. No actor transform, coordinate conversion to
EVEngine, geometry normalization or color fitting is applied. Output must not
already exist. This is source extraction data; a native runtime mesh importer is
still required. Future incompatible layouts require a new schema version.

## Original runtime star mip chain

`ue_star_mips.py` runs inside the reference Unreal editor. Set
`EVENGINE_STAR_MIP_OUTPUT` to a new writable output directory, then use
`-ExecutePythonScript=<absolute path to ue_star_mips.py>` with the reference project.
It creates an unsaved diagnostic material under `/Game/EveSkyReference/RuntimeReadback`,
waits for material preparation, samples explicit LODs 0--11 at native texel centers,
and reads linear pixels without exposure/tonemapping. The original texture stays
unchanged; its Sharpen1-generated and compressed runtime mip contents are preserved.

Each `mip-NN.rgba16f` contains tightly packed little-endian RGBA binary16; concatenate
these files in numeric order to produce the engine payload (44,739,240 bytes).
`settings.json` and `progress.json` record original texture controls and dimensions.
An `error.txt` means the export failed and must not be imported. Verify all 12
levels, byte sizes and finite values before publication. The engine schema-6
manifest declares `encoding: rgba16-float`, `mipLevels: 12`, width/height 2048,
and uses trilinear filtering. For reproducibility, repeat the export and require
all mip payload hashes to match, recording source asset and final payload hashes.

### Lunar mip readback

Run `ue_moon_mips.py` in the disposable UE project with `EVENGINE_MOON_TEXTURE`
set to `Moon_Color` or `Moon_PhaseNormal` and `EVENGINE_MOON_MIP_OUTPUT` set to a
new output directory. The helper reads all eleven native 1024-to-1 mip levels
as linear RGBA16F, rendering RGB and alpha separately through unlit diagnostics.
It records original texture settings and never saves changes to the source assets.

### Private packs / procedural sky

Do not commit imported UDS mesh or textures. Keep export output in an external pack directory.
See `examples/uds-sky/README.md` for the existing versioned pack layout and `run_sky.py --pack DIR`.
`run_sky.py` without a pack exercises the native deterministic procedural default. Python prepares
only a temporary game configuration; the engine itself generates default sky resources.
