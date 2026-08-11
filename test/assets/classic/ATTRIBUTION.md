# Classic graphics test scenes

These assets exercise lighting, shadows, and reflections the way graphics papers and engines typically do.

| Scene | Location | Size policy | Source / license |
|---|---|---|---|
| **Cornell Box** | `cornell/` (committed) | Tiny OBJ+MTL (~4 KB) | Public domain recreation by Guedis Cardenas & Morgan McGuire (2011), via [McGuire / Williams College data](http://graphics.cs.williams.edu/data) and [Cornell Box data](http://www.graphics.cornell.edu/online/box/data.html) |
| **Crytek Sponza** | `sponza/` (downloaded) | ~53 MB glTF | [Khronos glTF Sample Assets — Sponza](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza); original atrium by Frank Meinl / Crytek (see model LICENSE) |
| **Damaged Helmet** | `damaged_helmet/` (downloaded) | ~4 MB glTF | [Khronos glTF Sample Assets — DamagedHelmet](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet) |

## Download large scenes

```bash
scripts/download_classic_scenes.sh
```

Or let CMake fetch them when configuring/building tests:

```bash
cmake -DEVENGINE_DOWNLOAD_CLASSIC_SCENES=ON ...
# or
cmake --build <build-dir> --target download_classic_scenes
```

Downloaded trees are gitignored. Re-run the script anytime; it skips files that already match the expected size.

## Cite / credit

When publishing results that use Sponza or the McGuire archive packaging:

> Morgan McGuire, Computer Graphics Archive, July 2017 (https://casual-effects.com/data)
