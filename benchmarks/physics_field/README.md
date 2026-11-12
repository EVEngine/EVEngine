# Long-range field numerical benchmark

The separate [Vulkan GPU probe](gpu_probe/README.md) measures a device-resident
two-stage special case. It does not replace this general numerical baseline.

This standalone harness compiles the production `physics_field` sources. It does
not declare engine modules, start an engine loop, or measure a rendered frame.
It is a CPU field baseline, **not acceptance evidence for 60 FPS at 1080p**.

Initialize `external/zeroerr` first. Configure Release with the repository MSVC
environment wrapper on Windows:

```powershell
cmd /c cmake\with-msvc.cmd cmake -S benchmarks/physics_field -B build/field-benchmark -G Ninja -DCMAKE_BUILD_TYPE=Release
cmd /c cmake\with-msvc.cmd cmake --build build/field-benchmark --parallel 4
build/field-benchmark/field_benchmark.exe 10000 3 uniform
```

On Linux use the same CMake commands without the wrapper and the executable
without `.exe`. `EVE_ZEROERR_SOURCE_DIR` may point to an initialized checkout.
For reproducible focused tests, configure a separate Debug build with
`-DEVE_FIELD_CONTRACT_TESTS=ON`, build, and run
`ctest --test-dir build/field-contract --output-on-failure` (using that build
directory). Add `-DEVE_FIELD_PROBE_SHARED=ON` to test DLL exports/imports.
This probe links actual sources and existing test files; it does not substitute
for the engine's own manifest-driven SHARED/OBJECT builds or trim acceptance.
The contract target also covers the independent coordinate/frame and conservative
recursive-definition satellites. The timed executable still measures only fields.
Inputs are independently seeded point sources/targets, total mass one, G=1,
positions in approximately [-1,1], Taylor order 4, analytic acceleration
truncation tolerance 1e-3. This does not bound floating-point roundoff.

Arguments: source count (1..1,000,000), repetitions (1..1000), distribution
(`uniform`, `clusters`, `sheet`), optional expansion order (2..6, default 4),
and optional leaf capacity (1..1024, default 16). CSV records both parameters;
accuracy remains 1e-3 in every configuration. CSV includes every repetition, including the
first cold build; no warm-up is silently discarded. Budget rejection is a failed
run (exit 1), never a successful timing. Invalid CLI arguments return 2.
Stage A includes input copying, normalization, tree construction, interaction
admission, expansion translation and propagation. Stage B evaluates all targets.
Input generation, CSV, checksum, destruction, integration, collisions,
generation, visibility, upload and rendering are outside these field timings.
The whole-frame target must account for all those costs separately.

Two optional trailing arguments enable independent targets and reference checks:

```powershell
# 1,000 sources and 2,000 independent targets, with sampled reference verification
build/field-benchmark/field_benchmark.exe 1000 1 uniform 6 16 2000 verify
# Original source-as-target path, with reference verification
build/field-benchmark/field_benchmark.exe 1000 1 uniform 6 16 - verify
```

The target count accepts 0..1,000,000; `-` or omission retains the original path.
Independent targets use the named seed 20261027 and the same distribution family;
source generation retains seed 20261026 and its previous stream. Their scope is
`field_only_independent`; `N` remains source count and two additional CSV columns
record `target_count` and `target_seed`. The original mode's CSV columns are unchanged.
Both trees and copied batches count toward admission: maxParticles=Ns+Nt,
maxDirectPairs=(Ns+Nt)*1024, maxInteractions=(Ns+Nt)*512. Original mode retains
its former N-based budgets. Empty targets produce an empty output, not a claim
that any physical target was updated.

`verify` selects up to 64 evenly spaced target input indices, evaluates every source
for each with compensated summation, and reports results on stderr. It checks each
acceleration error against that target's analytic truncation bound plus
`1e-10*max(1, reference acceleration norm)`; bounds must also lie in [0,1e-3].
Potential error is reported but has no separate analytic-bound acceptance test.
Nonfinite reference/sample results or an exceeded acceleration allowance return 3.
This is sampled numerical evidence with an explicit roundoff allowance, not a
certified total-error bound for every target. A zero-target verification reports
zero samples. Reference work is timed separately as `verification_ms` after the
field timing; it is not hidden inside stage A/B. Verification between repetitions
can affect cache/thermal state, so record whether it was enabled in comparisons.
