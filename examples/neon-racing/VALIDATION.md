# Validation

Validated on Linux with the current engine sources, GCC Debug, software Vulkan (Lavapipe), Xvfb and null audio.
The local `build/cloud-minimal` configuration enabled vehicle, physics, model3d, procgen and font; Jolt was disabled because this example uses Box2D.

- Engine build: `cmake --build build/cloud-minimal --target eve -j 4` passed.
- EveScript compilation: `sq_static -c` passed.
- `python3 scripts/check_examples.py`: 157 examples, layout passed.
- `make check`: passed, including 313 repository script tests.
- Smoke: `MIN_RUN_SECONDS=2 RUN_SECONDS=16 ... scripts/smoke_examples.sh neon-racing` passed the full 16-second window.
- `python3 verify_runtime.py --port 7537`: passed acceleration, steering, braking, physics recovery, outer guardrail collision and a complete ordered checkpoint lap.
- Observed acceleration to 19.73 m/s, braking to zero, steering to 25.48 degrees, recovery to `(0, -48)` with zero speed, and lap time 20.60 seconds.
- Engine-owned readback captures: [chase](captures/chase.png), [overview](captures/overview.png).
- Full telemetry and completed development evidence session: [runtime-validation.json](captures/runtime-validation.json).

The only engine source correction is the missing `<algorithm>` include required by `ProcgenInstances.cpp` for `std::find_if`. No module API or architecture refactor was needed.
This is a flat arcade driving example with 2D collision mapped into a 3D presentation; suspension, tire slip and animated wheels are outside its scope.

## Native Dawn CI dependency fix

The initial Windows run passed the main build and unit tests, then failed while configuring native Dawn because its unchecked shallow checkout left SPIRV-Headers without `CMakeLists.txt`.
Native Dawn configuration now reads the exact SPIRV-Headers commit from Dawn's `DEPS`, fetches that commit as a GitHub archive, verifies its CMake file and core header, and supplies the same directory to Dawn and SPIRV-Tools before configuring either.
The pinned archive (`0d25db97cb9b8f725e4c95e4553001710e7fc39d`) was downloaded and configured with real CMake; cached reconfiguration also passed. Three offline CMake regression tests cover successful setup, incomplete content, and an unpinned revision. The Windows lane itself must pass on GitHub Actions after the fix is pushed.
