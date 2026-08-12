# Classic graphics test scenes

These assets exercise lighting, shadows, and reflections the way graphics papers and engines typically do.

| Scene | Location | Size policy | Source / license |
|---|---|---|---|
| **Cornell Box** | `cornell/` (committed) | Tiny OBJ+MTL (~4 KB) | Public domain recreation by Guedis Cardenas & Morgan McGuire (2011), via [McGuire / Williams College data](http://graphics.cs.williams.edu/data) and [Cornell Box data](http://www.graphics.cornell.edu/online/box/data.html) |
| **Crytek Sponza** | `sponza/` (downloaded) | ~53 MB glTF | [Khronos glTF Sample Assets — Sponza](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza); original atrium by Frank Meinl / Crytek (see model LICENSE) |
| **Damaged Helmet** | `damaged_helmet/` (downloaded) | ~4 MB glTF | [Khronos glTF Sample Assets — DamagedHelmet](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet) |
| **SciFi Helmet** | `scifi_helmet/` (downloaded) | ~29 MB glTF | [Khronos — SciFiHelmet](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SciFiHelmet) |
| **Flight Helmet** | `flight_helmet/` (downloaded) | ~47 MB glTF | [Khronos — FlightHelmet](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/FlightHelmet) |
| **Boom Box** | `boom_box/` (downloaded) | ~11 MB glTF | [Khronos — BoomBox](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/BoomBox) |
| **MetalRoughSpheres** | `metal_rough_spheres/` (downloaded) | ~11 MB glTF | [Khronos — MetalRoughSpheres](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/MetalRoughSpheres) |
| **Suzanne** | `suzanne/` (downloaded) | ~2 MB glTF | [Khronos — Suzanne](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Suzanne) |
| **Duck** | `duck/` (downloaded) | ~130 KB glTF | [Khronos — Duck](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Duck) |
| **Avocado** | `avocado/` (downloaded) | ~8 MB glTF | [Khronos — Avocado](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Avocado) |
| **Water Bottle** | `water_bottle/` (downloaded) | ~5 MB glTF | [Khronos — WaterBottle](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/WaterBottle) |
| **Lantern** | `lantern/` (downloaded) | ~5 MB glTF | [Khronos — Lantern](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Lantern) |
| **Antique Camera** | `antique_camera/` (downloaded) | ~12 MB glTF | [Khronos — AntiqueCamera](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AntiqueCamera) |
| **Cesium Milk Truck** | `cesium_milk_truck/` (downloaded) | ~2 MB glTF | [Khronos — CesiumMilkTruck](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMilkTruck) |
| **Barramundi Fish** | `barramundi_fish/` (downloaded) | ~4 MB glTF | [Khronos — BarramundiFish](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/BarramundiFish) |
| **Corset** | `corset/` (downloaded) | ~9 MB glTF | [Khronos — Corset](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Corset) |

## Download large scenes

```bash
scripts/download_classic_scenes.sh
# or
make download-classic-scenes
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

Khronos sample assets: see each model's LICENSE under the [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) repository.
