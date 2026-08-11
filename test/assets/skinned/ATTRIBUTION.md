# Skinned character test assets

| Asset | Location | Size | Source / license |
|---|---|---|---|
| **CesiumMan** | `cesium_man/` (downloaded) | ~0.5 MB glTF | [Khronos glTF Sample Assets — CesiumMan](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMan); original by Cesium (CC-BY 4.0) |

CesiumMan is a free skinned walking human used by `animation.skinned.*` tests to exercise Assimp skin import, `AnimSkeleton`/`AnimClip` playback, and CPU linear-blend skinning (`AnimSkin`).

## Download

```bash
scripts/download_skinned_character.sh
# or
make download-skinned-character
```

Or let CMake fetch during the test build (enabled by default):

```bash
cmake -DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=ON ...
# or
cmake --build <build-dir> --target download_skinned_character
```

Downloaded trees are gitignored. Re-run the script anytime; it skips files that already match the expected size.
