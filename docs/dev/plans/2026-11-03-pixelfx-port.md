# Native procedural PixelFX port

## Scope

Port all244 distinct effects from patch-gg/pixelfx revision
`a84159688af3ca37a4be05a0d422b94451b2e24e` into the existing Stylize module.
The native runtime uses C++ algorithms, existing engine math and the existing dependency
pin. The upstream JavaScript is used by independent reference tooling.

Deliverables:

- Native implementations and source-owned descriptors for244 unique effect IDs.
- Typed configurable options, introspection and atomic validated configuration publication.
- Owning prepared programs, independent simulation/RNG state, checked advance/seek/replay.
- Default and custom actor pixels/masks/anchors, stock rig, targets, body occlusion and12 moves.
- Source Act orchestration, windup/hits/hitstop/recovery, ghosts and shake.
- Runnable searchable gallery with default/variant previews, parameter editing, timeline,
  playback/stop, body modes, targets, color/light layers, backgrounds, mirror and rotation.
- Independent inventory, parameter and color/alpha/light comparison tooling.
- License attribution and user/API documentation.

## Ownership, time and interfaces

Effect instances own their settings, prepared geometry, mutable state and planes. Configuration
and replay candidates publish atomically and do not retain caller arrays or temporary actors.
Prepared frames and pose-cache sessions have explicit owning lifetimes. Public operations use
structured Result diagnostics; expensive preparation is separate from publication and queries.
Callbacks run synchronously with documented reentrancy checks and no held engine lock.

Analytic effects evaluate injected absolute time. Simulations retain fixed1/120-second substeps
and named effect RNG progression. Source Acts retain raw caller-update ordering and source
hitstop/target behavior; replay records successful step/stop commands rather than reconstructing
frame-dependent behavior from wall time. The bounded source pose cache preserves first-winner
rounded-pose history within its owning render session.

The existing Stylize module owns declarations, script bindings and tests. This adds no universal
ECS/gameplay root, damage authority, module boot channel or new external numerical dependency.
The gallery reuses two persistent color/light surface pairs and prepares only the selected pair.

## Reference fidelity and known limits

Inventory coverage must match the exact pinned244 IDs with no placeholder aliases.
Reference tools run the pinned upstream source independently and compare actual RGBA/light
planes, actor visibility and trajectories. Pixel coverage and alpha/light checks remain explicit;
a numerical similarity score alone is not visual parity proof.

The implementation deliberately uses existing engine/platform mathematical functions. Small
floating-point differences can change RGB rounding or the winning sample at raster/depth ties.
Known diagnostic cases include SourceRig overlap/raster edges and several GCC RGB rounding
samples. At an occlusion tie, a different winning material can produce a larger RGB delta at an
individual pixel; this must be distinguished from an incorrect effect algorithm. Full universal
bitwise equality across platform math implementations is not claimed. Canonical reference-frame
expectations and tolerances are not silently rewritten to hide a discrepancy.

Runtime sources, owning test translation units and reference probes share the source-scoped
`pixelfx/NumericContract.cmake` policy: preserve authored binary64 operation order without FMA
contraction. Header-defined arithmetic must not be compiled under a different mode in a test
or probe: a fused inline definition can also be selected by the linker for a runtime call.
The macOS half-plane failure was reproduced on the original Windows/WSL host: contraction
changed the strict `-2.375` vertex to `-2.3749999999999929`. Enabling contraction only in tests
reproduced151 failures,150 overlapping the160 macOS failures, including identical FireballThrow
hashes. This is a compilation-contract repair, not a new numerical tolerance or oracle change.

The remaining macOS Poof failure was captured losslessly at tick24: four color pixels differed
in position/alpha while the entire light plane matched. Moving only the floor-capped speed-line
endpoint across `y=-2` in the pristine upstream painter reproduced the complete macOS color
plane byte-for-byte. Darwin-target Clang output combines adjacent sin/cos calls into
`__sincos_stret`, whereas the radius constructor calls sin alone. Reusing libm's constructor
sine alone did not resolve the actual macOS failure. The strict repair evaluates downward
construction-angle sine with an independently authored compensated binary64 series: high/low
pi reduction,20 Taylor correction steps and explicit fma product-error recovery. It runs only during
construction; floor-limited rays retain that same computed value, and other rays keep their
original draw-time math. It introduces no dependency implementation or numerical library.
The regression pins eight original full-frame color/light pairs, including seed4294967295
whose source raster would change under exact floor clipping. All original reference hashes
and strict alpha/geometry assertions remain unchanged. Seven Poof cases/2724 assertions pass
under GCC, Clang and both MSVC linkage routes. GCC and MSVC each match the original source on
all132 frames/264 planes for four seeds. A separate6192-direction diagnostic contains2025
floor-limited rays; the compensated calculation introduces no additional endpoint-row
disagreement relative to the existing GCC baseline (both retain20 disagreements with V8).
That direction-only observation is not a claim of full-frame or universal source parity.
Hosted macOS at292e5a91e9 passed all seven original Poof cases, including the strict
floor-row regression, with their original hashes. The separately measured
exact-floor semantic candidate and its four reference updates remain unapplied.

On 2026-11-09 the user explicitly approved the first three measured RGB quantization boundaries,
then separately approved the additional BlizzardVortex frame exposed by completing the suite.
Each boundary is confined to its frame and the exact pixels/channels below. The pinned upstream
reference export was regenerated and compared with the current GCC native output. Only the
observed directional one-level channel difference is admitted; alpha and every other channel/pixel
remain exact. The original full-frame hashes are retained: after checking the measured pixel,
the comparison hashes its canonical reference value with all other actual pixels unchanged.
Light-plane assertions remain exact.

- BlizzardVortex, default seed17, tick48 (`0.4s`), 128x116 pixel(71,64): G235/236.
- BlizzardVortex, default seed17, tick120 (`1.0s`), 128x116: pixel(56,89) B254/255,
  pixel(55,90) B255/254, pixel(55,96) G235/236. All three alpha values remain exact.
- ScytheReap, seed0, tick49 (`0.4083333333333333s`), 184x116 pixel(142,60): R215/216, B241/242.
- EarthStomp, default seed1, tick166 (`1.3833333333333333s`), 290x142 pixel(181,124): G105/106;
  alpha remains85.

On 2026-11-11 the user explicitly approved ten additional measured pixel positions.
Full color/light planes from hosted macOS at1cbd4f5c62 and292e5a91e9 reproduce identical
differences against the pinned source. The same exact position/channel/direction contract
and original hashes apply; no other frame, seed, palette, alpha or light tolerance is added.

- FeatherBurst default, tick4: pixel8887(87,55), reference0xfffaf3f3, R243 ->244.
- BlizzardVortex default, tick0 and its exact tick240 loop repeat: pixel9151(63,71),
  reference0xfff4c862, R98 ->97.
- BlizzardVortex default, tick12: pixel4035(67,31), reference0xffffeca6, G236 ->235;
  pixel12730(58,99), reference0x6cfef4c9, B254 ->255;
  pixel13113(57,102), reference0xfffffadb, R219 ->220;
  pixel13114(58,102), reference0xfffffadc, G250 ->251.
- BlizzardVortex default, tick72: pixel12345(57,96), reference0x74fff1c0, B255 ->254.
- ScytheReap seed123, original palette[4279373632,4281862768,4286604976,4292677584,4293980400],
  time0.55s: pixels13570(138,73),13572(140,73),13574(142,73), reference0x58dd0fd0,
  R208 ->207 at each; alpha88 remains exact.

Counterexamples must reject a two-level difference, changes to an unselected channel,
alpha, or any neighboring pixel, as well as an invalid reference index/channel mask.

Pristine ShadowTendrils seed3 reaches a nonfinite line-width path around tick59 and can loop
indefinitely upstream. The native finite-input guard is documented as PIXELFX-SOURCE-001;
reference evidence before that point and alternate-seed dense coverage are retained locally.

The numerical-library experiment in EVEngine/third-party PR10 was closed at user request and
is not consumed by this change. Source, prebuilt and SDK dependency ownership remain unchanged.

Pinned dependency release transfers retry transient network/5xx failures at most three times
per file, on the same official URL, with30-second network-operation timeouts and at most60 seconds of
server-directed/backoff waits. Partial files are truncated before retry; SHA256 and package
identity checks still precede installation. HTTP401/403/429 and certificate verification
failures remain fatal and cannot select another route or AUTO source acquisition.

The WebGPU SDK acquisition step feeds its already verified Dawn archive into Emscripten's
normal port source cache before compilation, instead of downloading the identical archive
again during prewarm. Both the existing SHA256 lock and Emscripten's upstream SHA512 identity
are checked. Cache publication is staged; existing mismatched contents are rejected rather
than overwritten. No dependency code, headers, include paths or compiler tests are replaced.
The unmodified Emscripten6.0.9 loader (source4e4223852a0835923411059a3929907d7df1232e)
loaded the real locked port from that cache with zero network requests in a local transport
contract check. This is cache-loading evidence; actual WebGPU compile/browser checks remain
required in CI.

Android retains Gradle8.5 and uses its official downloads.gradle.org distribution entry point
in both the APK wrapper and the SDK tool default. The APK wrapper also pins the official
SHA256 `9d926787066a081739e8200858338b4a69e837c3a821a33aca9db09dd4a41026`.
An entire132519731-byte transfer through that entry point matched the digest locally without
retaining the archive. No Gradle wrapper implementation or version is changed.

On a strict frame failure, diagnostic sampling uses a separate instance with the same settings
to inspect every pinned time in that scenario before the original assertion aborts. Existing
expected hashes and fatal assertions remain unchanged. A local seeded two-frame probe verified
all four diagnostic planes against the reference and verified that the original instance's
color, light and seed remain unchanged. Access-denial logs expose only rate-reset metadata;
they never retry a denial or print signed URLs/authorization headers.

### Animation dependency-prefix selection during CI reconfiguration

The Windows debug test step reconfigured a source-built dependency setup to the pinned
prebuilt package. Animation's private guessed `build/third-party-binary` include roots
still preceded the selected package and loaded the previous Poco headers, producing
C4244 errors under the unchanged strict warning policy. Animation now inherits the
selected install prefix from `eve_engine_includes`; its source-only Assimp bridge roots
are enabled only for source dependencies. No dependency sources or warning gates change.

### Jellyfish Release random-draw sequencing

Hosted Windows x64 and ARM64 Release at633d27d82e each failed the original Jellyfish
tick29 frame (color2359401437 instead of151853085). The original-host MSVC Release
CPU probe reproduced that exact hash. Full-frame comparison found displaced bubble
pixels, alpha and light differences, so this is outside every approved RGB boundary.

The jet-bubble x expression contained both `r.range(0,4)` and `r.range(4,10)` as
unsequenced arithmetic operands. JavaScript consumes these draws left-to-right; C++
does not specify that operand order. Storing offset and speed in separate initializers
restored all25 existing strict phase frames, including the failing hash, without
changing references. The two analogous ambient-particle expressions in the same kernel
also store their draws in source order. RNG implementation, seed, stream count,
parameters and all reference assertions remain unchanged. No optimization flag,
dependency implementation or additional tolerance is changed.

### Preserve Ubuntu archive HTTPS during CI provisioning

Atf94cd1444c, Linux SDK and WebGPU SDK provisioning exhausted all three bounded
APT update attempts against `http://archive.ubuntu.com`, before any compilation.
The same official noble InRelease index returned200 over verified HTTPS locally.
The SDK workflows and two remaining Release provisioning paths now normalize to
`https://archive.ubuntu.com`, matching the existing HTTPS native CI paths. Actual
Linux sed fixtures checked idempotence and preserved suites, components, signing-key
configuration and unrelated repositories. Only four replacement URL protocols change;
APT signature/TLS checks, package lists, retry/time limits and all build/test gates remain.

The subsequent HTTPS attempts also timed out, and both fuzz jobs explicitly reported
temporary Ubuntu mirror DNS resolution failures. Ubuntu's published mirror guidance
recommends registered archive replicas for this availability problem. The shared CI
normalizer therefore selects the registered Kernel.org Ubuntu archive replica over HTTPS,
including the previously unnormalized fuzz/profile preparation paths. It changes only
Ubuntu archive/security URI roots. Other repositories, suites, components,
the existing signing key and validity policy remain unchanged; ARM ports are handled
separately below. No DNS resolver, host
mapping, credentials, package implementation or test gate is replaced.

Local HTTPS retrieval of noble, noble-updates, noble-backports and noble-security
InRelease files returned200 from mirrors.edge.kernel.org. All four passed real gpgv
verification with the existing Ubuntu archive keyring and signing key
F6ECB3762474EDA9D21B7022871920D1991BC93C. Source:
https://launchpad.net/ubuntu/+archivemirrors (Kernel.org),
https://mirrors.kernel.org/ (Ubuntu service), and
https://discourse.ubuntu.com/t/80500 (Ubuntu mirror guidance).
An isolated real APT update with error-on-any-failure also refreshed all four suites
successfully (695kB in13 seconds), retaining the existing Ubuntu signing keyring.
Only metadata caches under ignored build/ were written; no packages were installed.

The live Linux ARM64 SDK page at8aef120410 subsequently showed the same temporary
DNS failure for ports.ubuntu.com, with its package-install step still running after
75 minutes. The initial archive-only normalizer intentionally left that distinct
architecture repository untouched. It now maps ports to the separately registered
UC Berkeley Ports HTTPS replica, never to the x64 archive. Four Noble Ports InRelease
files returned200, contained signed ARM64 package-index hashes and passed gpgv with
the same Ubuntu archive signing key. An isolated real ARM64 APT update with
error-on-any-failure refreshed all four suites successfully (694kB in4 seconds).
The native ARM SDK and test preparation paths use this shared normalizer, three
bounded update attempts and fatal partial-index errors; the SDK install step has
the existing native-test lane's25-minute bound. Package lists and all native
build/archive/consumer checks remain. Registry:
https://launchpad.net/ubuntu/+mirror/mirrors.ocf.berkeley.edu-archive2 .
The native SDK consumer, Linux package-runtime and extracted WebGPU consumer
preparation steps also invoke the same normalizer before APT. The source-quality
lane's isolated Noble source file uses the verified x64 replica directly, retaining
its Ubuntu key and suites. This closes the remaining preparation paths for the
applicable CI/SDK matrix rather than fixing only the initial SDK producer.

## Expanded preview regression follow-up

At79956d6ca8, ordinary CI and all nine SDK producer/consumer gates passed.
The expanded example smoke passed229 examples but crashed dungeon-generator-3d
and topdown-procmap at their first rendered frame. The exact hosted Linux runtime
reproduced the dungeon crash under local Lavapipe. Hiding the scene prevented the
crash; disabling shadows did not. A debugger-only control that preserved the first
bindless-table initialization and skipped later initializations let both original
examples present and continue, without changing their scripts or the executable.
The repeated initialization replaced GPU mesh/material buffers after their CPU
records had already been registered. The repair keeps these resources with their
existing Graphics/device owner instead of recreating them on each submission.
No public API, ECS shape, persistent format, module boundary or dependency source
changes are required. The existing native resident-submit contract now observes
actual red/blue pixels over repeated submissions and a material update, rather
than accepting only a successful submission status/draw count.

The headless profile separately failed while downloading system packages after
15.9MB had transferred: the registered HTTPS replica's IPv4 connection timed out.
Other profiles using the same signed repository passed. Profile installation now
has three bounded attempts with30-second transport timeouts, preserving the exact
package list, signature/hash checks and the existing15-minute step bound. Failure
after those attempts still fails the job. No profile, example or assertion is skipped.
Raw transcripts, the official runtime and isolated debugging probes remain local
under ignored build/pixelfx-validation.

## Intel macOS numerical diagnostics follow-up

The ec8a68213c native Intel Debug lane completed6122 cases and reported three
PixelFX failures: FeatherBurst, BlizzardVortex and BluntHit. FeatherBurst's
lossless frame transcript identifies one additional R244 ->243 pixel at(87,56)
on tick4; its light plane and all other pixels match the pinned source exactly.
That location is outside the approved ten-position boundary and is not admitted.
The other two fatal assertions did not retain complete subsequent-frame data.

Extend failure-only complete-scenario diagnostics to BluntHit and every
BlizzardVortex comparator branch, preserving all original hashes and approved
channel/direction/position boundaries. Two independent native Intel Debug/Release jobs run these same three
CPU-only contracts without waiting for the expensive native engine build, so
follow-up evidence arrives sooner. The complete engine/rendering jobs and their
original dependencies still run unchanged. No production pixels, oracle,
tolerance, test exclusion or dependency implementation changes are made here.

A separate x86_64 Rosetta reproduction on the existing Mac debug host confirms
the first BluntHit hash and reveals later one-channel rounding differences, but
has different FeatherBurst behavior and is not substituted for native Intel CI
evidence. Raw full planes and transcripts remain in ignored build/. Collect
native Intel complete-scenario evidence before proposing any additional boundary.

## Signed APT mirror failover follow-up

At e4123ec21a, Linux SDK job131069841677 and WebGPU Debug job131086758341
failed before compilation: Kernel.org IPv4 connections timed out and IPv6 was
unreachable. APT update returned success with stale indexes, so the bounded
update loop did not retry and installation subsequently exited100.

For hosted mirror+file sources, retain the normalized registered replica and
prefer the canonical HTTPS Ubuntu archive in the same mirror list, retaining
the registered replica as the alternative. Use
APT's built-in per-file mirror switching for both signed indexes and archives;
do not add a custom downloader or weaken TLS, signatures, validity or hashes.
Preserve the canonical alternative on repeated normalizer invocations. ARM ports
retain their separate archive and never receive the x64 alternative. SDK and
shared WebGPU update commands use --error-on=any so incomplete index refreshes
cannot satisfy the update loop with stale metadata.

Eight real-Bash contract cases cover idempotence, existing alternatives,
signature/validity/suite preservation, ARM separation and unrelated sources.
An isolated real APT run with an unreachable first HTTPS mirror fetched all four
Noble suite indexes from the canonical alternative, verified each InRelease with
the existing Ubuntu2018 keyring, and downloaded the real cmake-data package with
APT's hash validation. With all mirrors unreachable, the same old-index state
returned100 under --error-on=any. No packages were installed and no host sources
were changed. Raw evidence remains under ignored build/pixelfx-validation.
The18 additional Intel pixel positions were subsequently explicitly approved by the user; the exact scope is recorded below.

## Approved additional Intel macOS RGB boundaries

The user explicitly approved the18 additional positions (19 frame occurrences)
after reviewing the native Debug/Release evidence. Both configurations produced
28 byte-identical complete color/light planes. Only these default-configuration
frame/channel/direction boundaries join the previously approved ones:

- holy/feather-burst, tick4: (87,56), R244 ->243.
- ice/blizzard-vortex, tick0: (57,93), B254 ->255.
- ice/blizzard-vortex, tick4: (59,91), B247 ->248.
- ice/blizzard-vortex, tick120: (46,93), B255 ->254.
- ice/blizzard-vortex, tick120: (49,101), B254 ->255.
- ice/blizzard-vortex, tick144: (67,82), G199 ->200.
- ice/blizzard-vortex, tick240: (57,93), B254 ->255.
- impacts/blunt-hit, tick12: (69,43), G181 ->180.
- impacts/blunt-hit, tick18: (73,40), G177 ->176.
- impacts/blunt-hit, tick18: (73,41), R209 ->208.
- impacts/blunt-hit, tick24: (75,39), R195 ->194.
- impacts/blunt-hit, tick30: (79,56), R187 ->186.
- impacts/blunt-hit, tick42: (77,37), R185 ->186.
- impacts/blunt-hit, tick60: (78,34), B165 ->166.
- impacts/blunt-hit, tick60: (81,34), R182 ->183.
- impacts/blunt-hit, tick60: (75,36), G167 ->168.
- impacts/blunt-hit, tick60: (76,36), B167 ->168.
- impacts/blunt-hit, tick60: (74,37), R182 ->183.
- impacts/blunt-hit, tick60: (87,48), G167 ->168.

BlizzardVortex tick0 repeats exactly at240. Keep original reference hashes, exact
alpha/light and all unselected channels/pixels. No production kernel, RNG,
geometry, seed, material or output changes are made. The existing directional
comparator and its negative cases remain in force. Failure-only complete-scenario
diagnostics remain available if any different position or phase fails.

## Validation and review gates

Run against the final PR base/head:

1. `make check` for source quality, binding/example/API and architecture contracts.
2. Windows SHARED and OBJECT builds and focused PixelFX/domain tests.
3. Official `scripts/smoke_examples.sh pixelfx` and actual engine-owned screenshot checks.
4. Hosted platform CI and exact-log repair of relevant failures.
5. Native/reference default/variation, invalid-input atomicity, seeded replay, stop/end,
   actor/move and simulation trajectories; preserve the distinctions above.

Existing local evidence before the final dev refresh includes:

- Actual Windows SHARED and OBJECT builds and917 focused PixelFX cases on each route.
- Official native gallery smoke,72 angle inputs, three exact full-turn screenshot round trips,
  and no spill beyond preview panels.
- Three complete244-effect gallery selection cycles with bounded measured process memory
  after fixing returned-scope retention in the existing CallGraph owner.
- Actual native CPU measurements for24 representative scenarios/8640 frames; separate Debug
  CRT allocation observation and480-frame native texture-update latency observation.
- Read-only native particle-container observation for BigExplosion and ScytheReap with20
  unchanged actual color/light planes. Container occupancy, live records, geometry/light counts,
  allocations and GPU upload timing are reported as different quantities.

These measurements are Debug observations rather than release throughput guarantees. Validation
must be refreshed as required by final source/base changes; queued or running CI is pending.
Generated frames, JSON, probes, metrics, raw work logs and build outputs remain under ignored
`build/` and are not PR artifacts. No architecture exception is taken.
