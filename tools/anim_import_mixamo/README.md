# convert_mixamo

Offline helper: Assimp FBX → compact `.eva` fixtures for animation tests.

```bash
g++ -std=c++17 -O2 \
  -I$EVE/src/modules -I$EVE/src/engine -I$ASSIMP/include \
  $EVE/src/modules/animation/AnimSkeleton.cpp \
  $EVE/src/modules/animation/AnimPose.cpp \
  $EVE/src/modules/animation/AnimClip.cpp \
  $EVE/src/modules/animation/AnimImporter.cpp \
  convert_mixamo.cpp exception_stub.cpp \
  -lassimp -o convert_mixamo

./convert_mixamo Idle.fbx Idle.eva Idle
```

See `test/assets/mixamo/README.md` for fixture provenance.
