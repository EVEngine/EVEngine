# Vulkan two-stage field and integration probe

This is an isolated benchmark, not a shipping EVEngine backend or a full-world
acceptance test. It does not change the engine's default physics or graphics.

The probe keeps N source positions/masses and N target positions/velocities in
Vulkan storage buffers across frames. Source and target states each have an
input/candidate pair. Each frame has three compute dispatches:

1. `reduce.comp`: each 256-source workgroup applies the frame's bounded source
   displacement, writes the candidate source buffer, and accumulates mass and
   first moments into eight sign-of-coordinate spatial cells. Both source
   buffers stay GPU resident.
2. `final.comp`: eight workgroups reduce all partials to eight centroids/masses.
3. `evaluate.comp`: one invocation evaluates the eight monopoles and a
   conservative force-error bound per target, then writes a candidate
   semi-implicit Euler step at fixed `dt=1/60` when the bound is within 1e-3.

Two descriptor sets alternate input and candidate source/position/velocity buffers.
After the GPU fence and one admission-word readback, the host advances to the
candidate set only when every target was admitted. An invalid frame discards
the candidate without changing the published source or target input. This is an isolated probe
of transactional publication, not yet an engine simulation owner.

The generated sources form eight compact clusters near the corners of a unit
cube. Every target starts around x=10. The fixed radius 0.04 is checked against
all generated sources and their CPU reference centroids. Each frame alternates
a +0.001/-0.001 x step, with adjacent sources moving in opposite directions.
The CPU reference follows every source's published frame position. This
exercises a changing field equation and source state without
uploading the source array each frame. The bound uses the
Newtonian kernel's gradient norm, 2/r^3, and a minimum separation of d-radius:
sum over cells of 2*M*radius/(d-radius)^3. Near targets or those exceeding the
error budget reject visibly. The shader uses binary32; the reference checks
all eight moments, 16 directly summed target accelerations, and 16 integrated
target trajectories in binary64. The compute work is O(N) for fixed eight-cell
capacity; no target loops over the N sources.

```powershell
$env:PATH="$env:VULKAN_SDK\Bin;$env:PATH"
& cmake/with-msvc.cmd cmake -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -S benchmarks/physics_field/gpu_probe -B build/gpu-field-probe
& cmake/with-msvc.cmd cmake --build build/gpu-field-probe -j 4
& build/gpu-field-probe/gpu_field_probe.exe build/gpu-field-probe 1000000 120
& build/gpu-field-probe/gpu_field_probe.exe build/gpu-field-probe 1000 1 near
& build/gpu-field-probe/gpu_field_probe.exe build/gpu-field-probe 1000 1 mixed
```

The near and mixed invocations are expected to exit 1 with an explicit
approximation rejection. Mixed verifies that a far target wrote a candidate
while its published input remained unchanged. The reported wall time includes
command-buffer recording, submission,
status readback and a fence wait for each frame. It excludes initial upload,
Vulkan setup, the post-run CPU numerical reference, rendering,
collision, generation, device-loss recovery and any spatial hierarchy rebuild.
The buffers are host-visible and coherent for this probe; a shipping backend
will need device-local placement and explicit staging policies. The output is a
bounded-cluster throughput and synchronization measurement, **not** proof that
arbitrary million-body interactions fit 16.67 ms or satisfy the error contract.
The fixed generated clusters and their bounded two-state displacement do not model
arbitrary source motion. General distributions need an adaptive hierarchy, near interactions,
stale-state handling, and source/target revision checks.
