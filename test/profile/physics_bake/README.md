# Solver-free baked playback check

This fixture links the production CollapseAnimation codec/sampler and
FractureGeometry validator, with common Result/Status/Diagnostic and zeroerr.
It includes value-only capture assembly through `CollapseCaptureSource`, but omits
live-world capture adapters, World/Body, Box2D/box3d/Jolt, Squirrel and graphics;
it uses no function-section garbage collection to hide unresolved dependencies.
This proves the codec/sampler L2/L3 boundary, not complete module-manifest trimming.

Initialize the repository-pinned `external/zeroerr` submodule, then run:

```sh
cmake -S test/profile/physics_bake -B build/physics-bake-check -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build/physics-bake-check -j 4
ctest --test-dir build/physics-bake-check -LE bundle --output-on-failure
```

Windows uses the normal MSVC developer environment without the GCC overrides.
The fixture has not yet been validated on Windows. The ordinary domain suite
also discovers the same integration cases through test/test_levels.json.
Assertions/warnings fail CTest instead of silently accepting zeroerr's exit code.

Cases exercise v1/v2 migration to v3, noncryptographic content validation,
endpoint/state sampling, concurrent no-replace publication, existing-file
preservation, missing-parent errors, NUL paths and temporary cleanup.
The generated legacy bytes are an explicit independent format fixture, not
evidence of recording a live physical world. Live recording/rendering and
cross-process performance measurements belong to the full goal's composition tests.

The production `CollapsePlayback` implementation is also linked here. Its cases
cover shared immutable snapshots with independent clocks and placements, pause,
signed speed, endpoint seeks, reverse and multi-cycle looping, zero-duration
clips, invalid input and placement overflow recovery. This profile does not link
Squirrel or a renderer; VM ownership and depth submission are separate host tests.

To verify a real asset in a separate process with no solver/renderer/VM linked:

```sh
/tmp/evengine-1782-physics-bake/physics_bake_inspect /path/to/asset.evecollapse
```

The inspector samples two independent players over 120 times (one in reverse,
one translated), prints the recipe key, file size, pose count and CPU sampling time,
and exits nonzero on admission or sampling failure. Its timing excludes file load,
mesh upload and rendering; Debug timing is not release-performance evidence.

## Release channel measurements

The same standalone library also contains the production rigid, deformation and
particle codecs/players. Build a separate Release directory before measuring:

```sh
cmake -S test/profile/physics_bake -B build/physics-bake-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build/physics-bake-release -j 4
ctest --test-dir build/physics-bake-release -LE bundle --no-tests=error --output-on-failure
build/physics-bake-release/physics_bake_measure rigid /path/to/rigid.bake 16
build/physics-bake-release/physics_bake_measure deform /path/to/cloth.bake 16
build/physics-bake-release/physics_bake_measure deform /path/to/rope.bake 16
build/physics-bake-release/physics_bake_measure particles /path/to/particles.bake 16
```

The final argument is an instance count from 1 to 1024. All instances share one
immutable asset and own independent clocks/placements and any required scratch
buffers. A single caller output buffer is reused sequentially. After one warmup
sample per instance, each instance performs 2400 seek/sample queries, sweeping
240 points including both endpoints ten times. Odd instances traverse backwards.
The printed checksum consumes sampled positions to keep their work observable.

`load_ms` includes file admission/validation; `prepare_ms` includes instance and
output-buffer allocation; `sampling_ms` includes seeks, checked sampling and the
position checksum, but excludes allocation, IO, uploading and rendering.
`sampled_values` counts rigid poses, deform vertices or visible particles; these
units differ and should not be compared as equivalent workloads. Sampling totals
and `us_per_instance_query` describe CPU work, not GPU frame time or speedup over
live simulation. Load timings depend on the OS page cache; the tool does not flush
it. Run repeated fresh processes and report the build, inputs and aggregation.
On Linux, `/usr/bin/time -v` can measure whole-process peak RSS, which includes
loading and runtime/library overhead and is not asset-only or per-instance memory.
The standalone link is still narrower than full module-manifest trimming.

## Whole manifest module profile

`physics-bake-only` selects exactly `common`, `physics_bake` and `physics_bake_collapse` through the root
module manifest. It compiles and links their complete module object sets, rather
than the standalone fixture's curated source list. The engine foundation retains
its declared Squirrel dependency; live physics, particles, graphics, capture and
bake script satellites are absent. Adding those through a profile override fails
configuration instead of silently expanding the profile.

```sh
make profile/configure PROFILE=physics-bake-only PROFILE_BUILD_ROOT=/tmp/eve-bake-profile
make profile/build PROFILE=physics-bake-only PROFILE_BUILD_ROOT=/tmp/eve-bake-profile PROFILE_JOBS=4
make profile/smoke PROFILE=physics-bake-only PROFILE_BUILD_ROOT=/tmp/eve-bake-profile
```

Use `PROFILE_CMAKE_COMMAND='cmake -DEVENGINE_MODULE_LINKAGE=OBJECT'` at configure
time to exercise whole-object linkage explicitly; SHARED follows the usual group
library route. A prebuilt dependency directory can be supplied through the same
command using `-DEVENGINE_THIRD_PARTY_BINARY_DIR=/path/to/install`.

The smoke runner requires both capability probes and `eve_bake_playback_check`;
missing executables or failed runs are errors. The playback check saves and reloads
rigid/deform/particle fixtures, removes their files, and verifies observable samples
and instance independence. `profile/eve_bake_measure` accepts the same real-asset
arguments as `physics_bake_measure` above. A successful profile build proves L1;
its full-object link proves L2; running the smoke proves L3 and its expected sample
values provide L4 evidence for these native channels. The separate `profile/eve_bake_collapse_inspect` admits a real EVECOLL1 asset.
It does not establish live `physics_destruction` simulation, script/render composition,
or untested platforms. Inspect the actual selected modules, link command and
runtime dependencies before extending any claim beyond that scope.

## Whole-manifest script playback profile

`physics-bake-script-only` adds the complete `physics_bake_script` module to the
native profile's three modules. It constructs a real Squirrel VM and registers
`PhysicsBake` without live physics, particle simulation, graphics, recording
adapters, or the engine host. Run through the root build interface:

```sh
make profile/configure PROFILE=physics-bake-script-only
make profile/build PROFILE=physics-bake-script-only PROFILE_JOBS=4
make profile/smoke PROFILE=physics-bake-script-only
```

The smoke requires both capability probes, the native playback probe, and
`eve_bake_script_check`; a missing VM executable is a failure. The VM probe loads
recipe-matched rigid, deformation and particle assets, checks independent
instances, deletes the files, collects the original players, and verifies the
remaining players and owning snapshots. It uses fixture values for producer data;
real live recording is covered separately by domain tests.

Linux OBJECT and SHARED results are recorded in the implementation log. This
profile does not expose the legacy CollapseAnimation script class (its registration
still belongs to destruction), graphics submission, or recording bindings. Native
collapse playback remains included. It is not a standalone game host or evidence
of Windows/WebAssembly execution.

## Client rendering without live simulation

`physics-bake-render` starts from the normal minimal client selection and adds the
five bake runtime/script/presentation modules plus font and devtools (referenced
by graphics and the client command line). It intentionally keeps the client
host dependency closure rather than hand-selecting rendering implementation files.
It rejects live physics, cloth, rope, destruction, particle simulation, recording
adapters and solver third-party groups. The current closure has 58 modules.

```sh
make profile/configure PROFILE=physics-bake-render
make profile/build PROFILE=physics-bake-render PROFILE_JOBS=4
make profile/smoke PROFILE=physics-bake-render
```

The smoke runs five CTest cases in separate processes through
`eve_bake_render_check`: Graphics owner replacement, native deformation mesh lifecycle, scripted deformation,
scripted particle batching, and scripted rigid target submission. CTest must find
cases and each zeroerr result must report one passing case without failed assertions.
A display/Vulkan backend is required; on Linux CI use the documented software
Vulkan/Xvfb/null-audio environment. Pixel checks use engine readback, never Xvfb
screenshots. Build/runtime verification status is in the implementation log;
configuration or source-only matrix success alone does not establish L2/L3/L4.

### Fragment resampling diagnostic

The standalone build also produces `physics_bake_collapse_error`. Run it with
an existing collapse asset and an explicit reference cadence, for example:

```sh
/tmp/evengine-1782-physics-bake/physics_bake_collapse_error path/to/clip.evecollapse 60
```

It compares stride 1/2/4/8 recordings against that asset's sampler on the given
time grid, reports maximum/RMS translation and shortest-orientation error,
state mismatch counts, and the frame time/bone of the largest translation error.
It retains the final endpoint even when the stride does not divide the grid.
This is an asset-reference diagnostic, not a live-solver accuracy test or an
asset converter. It does not infer original timestamps from the frame count.
Stride 1 must reproduce the reference grid; sparse output may expose arbitrarily
large errors and is reported without an invented universal acceptance budget.
Do not reduce fragment cadence based only on RMS: discrete transitions and
rotation aliasing need separate acceptance. See the resampling report.

The Linux durability probe also races eight forked publishers for one new path.
Each writes a distinct 64 KiB payload after a pipe start barrier. Exactly one
publisher must succeed and seven must return Conflict; the final file must match
the successful writer byte for byte. This checks immutable cache publication
between processes in addition to interruption/failure injection. It does not
simulate power loss or establish the durability of non-Linux implementations.

### Native CI coverage

The main CI workflow includes `physics-bake-codec` on Ubuntu, Windows and macOS.
Each platform runs Debug and Release lanes. Each configures this standalone project and runs isolated
CTest cases with `--no-tests=error` (bundle cases excluded). Only the zeroerr
submodule is initialized; no solver, Vulkan SDK, display or engine third-party
bundle is required. Configure and CTest diagnostics are uploaded per platform.
The Linux-only syscall fault/interrupt probe remains Linux-only; other platforms
exercise their actual portable publisher, codec and playback paths. A configured
lane is not evidence of success until that platform's hosted run completes.
This static standalone lane also does not validate Windows engine DLL exports.

`physics_bake_allocation` is a separate executable that fails exactly the next
allocation while preparing a collapse player. It requires a checked failure,
unchanged existing playback, and a successful subsequent retry. Diagnostics may
allocate after the single injected fault; this does not promise recovery from
sustained process-wide exhaustion. Copying a by-value argument happens before
entry and may also throw. No production allocator or test hook is added.
