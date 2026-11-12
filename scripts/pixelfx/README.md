# PixelFX native/reference verification

These tools validate the native port; upstream JavaScript is only an independent reference.
Generated catalogues, frames, measurements and reports belong under build/.

Native CPU/ABI checks (Windows, from the repository root):

    cmake/with-msvc.cmd cmake -S scripts/pixelfx -B build/pixelfx-probe -G Ninja
    cmake/with-msvc.cmd cmake --build build/pixelfx-probe -j 4
    ctest --test-dir build/pixelfx-probe --output-on-failure
    build/pixelfx-probe/pixelfx_probe.exe --catalog build/pixelfx-reference/native.json

On other hosts use ordinary CMake configure/build commands. This target exercises the same
native implementation and module-owned cases, including a separate DLL consumer on Windows.
It does not substitute for the full engine build, relevant host domain, or gallery smoke.

Clone patch-gg/pixelfx locally and check out a84159688af3ca37a4be05a0d422b94451b2e24e.
The reference exporter rejects other revisions. No upstream clone or generated catalogue is committed.

    node scripts/pixelfx/reference.mjs --upstream PATH_TO_PINNED_CLONE --out build/pixelfx-reference
    python3 -X utf8 scripts/check_pixelfx_coverage.py --reference build/pixelfx-reference/catalog.json --native build/pixelfx-reference/native.json --report build/pixelfx-reference/coverage.json

Coverage exits nonzero until all 244 unique ids and their dimensions, origins, durations and
loop metadata match. It rejects missing effects, duplicate ids and unexpected aliases.
Catalogue evidence does not prove visual or parameter equivalence by itself.

Export an effect at nine fixed-tick sample times and compare spatial/color/alpha/light data:

    node scripts/pixelfx/reference.mjs --upstream PATH_TO_PINNED_CLONE --id holy/sunburst --out build/pixelfx-reference/sunburst
    build/pixelfx-probe/pixelfx_probe.exe --id holy/sunburst --out build/pixelfx-native/sunburst
    python3 -X utf8 scripts/compare_pixelfx_frames.py --reference build/pixelfx-reference/sunburst --native build/pixelfx-native/sunburst --report build/pixelfx-reference/sunburst-comparison.json

Sampling detail: `--frames N` distributes N samples over one declared effect duration,
rounding each time to a120-Hz tick. Increasing N beyond the number of ticks can
repeat sample times; it does not extend playback. For multiple cycles, pass the
same explicit `--times` JSON array to both exporters, for example all `i/120`
for `i=0..576` to cover two2.4-second cycles. Raw filenames use sample indices,
not ticks. Generate fixtures by reading each frame's `time` and filename in
`frames.json`, and verify distinct times/endpoints before claiming dense coverage.

The comparator defaults to exact per-pixel comparison and returns changed pixel counts,
alpha/color counts, bounding boxes and sample coordinates. Portable test statistics have a small
raster-boundary tolerance; they do not replace spatial comparison. Inspect endpoint differences:
native loop wrapping can differ from upstream floating endpoint evaluation. Actor-aware reference
rendering supplies the upstream default actor; the current prototype does not yet reproduce all
actor relighting.

Physical fire variations can be sampled with --effect-speed, --radius, --spread,
--reach and --pilot (comma-separated RGB hex colors); --seed and --height are also supported.
The native positive loop clock takes remainder directly, preserving exact absolute sample phases.

The probe prepares and commits owning configurations through the canonical Result API.
The static and DLL contract targets compile actual common Result/Status/Diagnostic sources;
configuration errors retain structured field paths and publication rejects consumed/wrong-kind tokens.

The native probe now admits overrides through the same versioned Value configuration codec.
The independent foundation targets compile canonical Value/JSON sources as well as Result/Status.

Simulation samples use owning prepareSeek/commitSeek and draw, never hidden replay in render.
--stone RRGGBB selects the scalar opaque stone color. The flame-pillar reference includes
sequential 120-Hz evolution; default and seed/height/stone variants are independent spatial comparisons.

The native probe also accepts `--wood RRGGBB,RRGGBB,...` for active wood ramps.
These values go through the same versioned configuration validator as engine callers.

Both reference.mjs and the native probe accept `--actor FILE` for a canonical actor JSON fixture.
The reference maps the fixture to the actual upstream stockActor/runtime actor shape; the native
probe submits it through the production configuration codec. This checks actor-aware effects as
full raw planes. Source none explicitly evaluates the pure reference effect without an actor.

Rig parity uses rig_reference.mjs (public posed/skeleton/shortfall from the pinned source) and
pixelfx_rig_probe --pose FILE --out FILE (production native solver, raster and pose decoder).
A pose fixture is an eve.pixelfx.pose v1 document. Run each reference pose in a fresh process
when testing raw evaluation, so upstream global quantized pose-cache aliases do not substitute a
previous pose. compare_pixelfx_rig.py --reference FILE --native FILE --report FILE checks full
sparse arrays, spatial cells, skeleton tolerances and mean depth; any mismatch returns nonzero.
The act cache/composed-timeline contract will need separate sequential-session validation.

Reproduce the complete authored-track oracle with:

    python scripts/verify_pixelfx_tracks.py --upstream PATH --probe build/pixelfx-probe/pixelfx_rig_probe.exe --out build/pixelfx-tracks

The tool requires the pinned clean upstream, checks every one of its 73 tracks, records the Node
version and writes raw reference/native/comparison JSON locally. A failing track makes the suite
fail. rig_probe also accepts --track FILE --times FILE --out FILE for explicit sample sets.
Rig snapshots now include all independently outlined parts, which the strict comparator checks
separately from the full body. The track schema is documented in the stylize PixelFX manual.


The native probe also accepts --options JSON to override any declared option through
its production versioned configuration decoder. Numeric packed RGBA ramps/colors use
0xAABBGGRR values expressed as decimal JSON integers; the source reference accepts the
same values. This avoids adding a per-effect command-line option channel.


Both raw-frame probes accept --frames N (2..4097; default 9). For exact 120Hz coverage
of a one-shot of duration D, use N=round(D*120)+1. For example pistol uses 217 and
shotgun 385. Reference --times JSON can instead request explicit ordered fixed-tick
times; it is mutually exclusive with --frames. Unknown reference flags reject.


The native probe now also accepts --times JSON, mutually exclusive with --frames.
Both require ordered nonnegative 1/120-second samples (1..4097 entries). Include D and
later times when auditing one-shots: upstream done() is advisory and does not disable
raw drawing or simulation updates. For example Magic Missile retains light at D=1.9;
--times '[1.8,1.9,2.0,2.1,2.25,2.5,3.0]' checks its endpoint and tail explicitly.


Frame comparison remains strict by default. An explicitly diagnosed floating raster
case can use `scripts/compare_pixelfx_frames.py --color-tolerance 1`: only RGB on the
color plane may differ by one quantization level; zero/nonzero coverage, alpha and
all light bytes must still match exactly. `exact` remains false and raw difference
counts, examples and bounds remain visible; `accepted` separately records the
selected contract. Document the numerical cause and measured scope for each use.
Nuke's Windows/Node exp height rounding is the first measured use. Do not report
accepted-with-tolerance output as byte-exact output.


New reference frames include `actorVisible`, evaluated from the source actor
alpha export at bounded/wrapped playback time. Native probes additionally write
`actor-visibility.json` (`eve.pixelfx-actor-visibility/v1`); unknown metadata keys
are ignored, but identity, sample times, counts and boolean values are strict.
The frame comparer requires matching native metadata when reference fields are
present. Older v1 plane-only references report `actorVisibility.available=false`
and make no body-policy parity claim. Visibility mismatches fail independently
of any explicitly chosen RGB raster tolerance.


Track-probe v1 reports are extensible: readers ignore unknown report properties.
The additive presence array lists source properties whose value is defined;
undefinedProperties in reference reports records explicit JavaScript undefined
keys separately, which JSON would otherwise omit. Native reports use the typed
PoseField metadata order; compare presence as a set. The verifier regenerates
both sides and requires defined-property agreement alongside existing pose/joint/
shortfall checks. Older report readers continue to ignore these extra fields.


Act clock/effect-layer observers use act_reference.mjs (pristine createAct) and
pixelfx_act_probe (production native ActRuntime). Both accept --id, --out, --input
and optional --seed. The reference also needs --upstream. This observer explicitly
uses body:false,target:false; it does not prove full body/target composition.
Input documents have schema eve.pixelfx.act-probe-input, version1, closed fields:
steps (at most4096finite deltas in0..1s), frames/stops (indices in0..steps.length),
and optional initialColor/initialLight uint32 values defaulting to zero. Unknown
fields/versions reject. Output eve.pixelfx.act-clock-probe/v1 reports allow unknown
additive fields; states report source clocks/pose/hit timestamps, while frames.json
uses the existing raw-plane comparison contract. Native prepare/publish is two-step.

Generate authored Act data explicitly with generate_act_data.mjs --upstream PATH
--out src/modules/stylize/pixelfx/ActData.cpp, then clang-format the new source.
The importer validates source pin/cleanliness and244native effect identities;
this is an authoring operation and never runs during configure or gameplay.

Act observations additionally contain `targets`: source spring/flash/tint/shiver clocks, uint32 RNG state, and owning per-observation straw records. The native runtime lends these states read-only until the next step; the observers serialize copies. This proves target simulation independently of the still-pending target/body image compositor.

The Act probe defaults to effect-only layers. `--targets 1` selects exact source `body:false,target:true` composition; `--targets 0` omits targets/straw. Both tools support `--flip 0|1`, finite decimal `--x/--y` (absolute bound 4096), and integer `--width/--height` (1..4096), `--origin-x/--origin-y` (-4096..4096). These are observation CLI options; the version-1 closed input file schema remains unchanged. Full body composition is still a separate implementation/verification stage.

`--body 1` selects the owning full-body frame builder, including stock onBody composition (default `--body 0` preserves prior probes). Stock onBody source matrices pass, while five normal-body effects still have open numerical parity differences; custom carrying and full presentation integration remain incomplete. The builder copies its input background and returns an independent frame token, retaining no input pointers. Source control, raster comparison and full delivery acceptance remain separate evidence.

To observe actual cross-Act source cache history, pass `--warm-id ID --warm-steps N` with `--body 1` (N:0..4096). Both options are required together. The warm Act advances at1/60s and draws once before the requested Act's sampled frames; native uses the same explicit `ActRenderSession`. Without warm options the observer still creates an isolated session. Native also emits versioned `session-stats.json` counters; these are measurement inputs, not a speedup claim. Context history and update/draw ordering are part of replay.

`--contract 1` additionally writes `contract.json` (schema
`eve.pixelfx.act-contract/v1`) before observations. It compares pristine
`actTiming` and `actBounds` with native `timing()` and `prepareBounds()`.
Timing preserves authored pose/hit order, defined pose fields, absent versus
explicit-null skin, and source timing defaults. The returned native view borrows
immutable data until Act destruction. Bounds are an owning completed value for
the default source canvas, authored poses and target margins; they do not promise
arbitrary custom-actor or transformed extents. Bounds preparation warms the shared
pose cache, so replay must preserve bounds/frame call order. Neither operation
advances simulation time or RNG. All244 effects currently match these contracts
strictly (73 authored Acts,595 pose keys,89 hits).

Native `ActRuntime::step(delta, observer)` supports synchronous hit observation
at the pristine source phase. It lends the authored hit Value and timestamp only
for that invocation; copy them to retain. Querying state and stopping are allowed,
recursive stepping is rejected, and the Act must outlive the call. Exceptions
propagate after partial source progress, leaving the throwing hit pending. The
observer is never retained. This native primitive still needs live script/gallery
wiring and broader callback matrix verification.

Both Act observers accept `--actor PATH` in the canonical versioned
`eve.pixelfx.actor` format (also applied to the optional warm Act). Native input
uses the sole actor decoder. Pristine full-body drawing selects its built-in
sprite from actor scale/k and uses actor x/y, even for custom mask/pixel inputs.
The strict custom-input matrix verifies that source behavior; it does not prove
native custom-pixel full-body presentation, which remains a separate requirement.

The pristine Act reference rejects native `source:none` explicitly: source option
resolution restores a stock actor for null/missing actor input, so it cannot
represent native None equivalently. No source default is silently changed for
this comparison. Hero and Dummy controls each pass12 strict full-body planes.

Native-only `--input-body 1 --body 1` selects actual owned actor RGBA through
`ActBodyPresentation::InputActor`; the default remains pristine SourceRig.
Arbitrary images follow authored translation and upper-row lean/crouch shear,
without inferred skeleton limbs. Mask and RGBA remain independent. Body, ghosts
and onBody color transfer use the actual image; fractional local cells floor and
placement rounds before mirroring. Frame preparation does not advance time/RNG
or warm the source rig cache. Captured frames survive runtime/session destruction.
Allocation may throw `std::bad_alloc`, as for the source owning frame builder.
The pristine reference deliberately offers no equivalent flag: this is a native
presentation extension. Native smoke does not replace independent visual checks.

`input_actor_reference.mjs` takes the same upstream/id/input/actor/out and
canvas/placement/seed flags as the Act tools and independently composes actual
custom-image presentation using pristine source clocks, poses and effect kernels.
It includes external-effect follow/floor weights, visibility, dust and ghosts;
targets are outside this oracle. It never alters source modules and does not
label this adapter as upstream's stock-body renderer. `oracle.json` uses
`eve.pixelfx.input-actor-oracle/v1`; readers ignore unknown additive fields.
The body-aware37-effect matrix and Heavy-landing ghost samples compare raw planes
strictly. Active-carry fingerprints in the native Act suite use this independent
adapter. Native `--input-body 1` remains a distinct explicit presentation mode.

The engine now exposes owned EveScript Act/session/preparation/frame/bounds
objects through the existing stylize binding provider. See the runtime examples
and lifetime/cost contracts in `docs/usr/modules/stylize-pixelfx.md`. A real-VM
integration suite under `test/integration/pixelfx/` covers owner destruction and
all244 IDs through metadata, step and actual-image frame presentation. This is
separate from pristine raw-plane observers and native-window visual evidence.

The live Act workbench now uses descriptor discovery and only two selected Act
owners, reusing fixed display surfaces. Script frame `width`/`height` optionally
select a smaller logical canvas while full-size RGBA8 outputs receive transparent
padding. `body:none` delegates native source body:false composition. See the
example README for60Hz/replay bounds and current controls; legacy page commands
are preserved only as explicitly historical notes. Gallery captures and traversal
logs remain ignored under build/pixelfx-validation.
