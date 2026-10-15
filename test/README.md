# C++ test levels

The complete catalogue is [test_levels.json](test_levels.json). It covers every
C++ source under `test/`, including standalone projects, profile checks, fuzzing,
helpers and manual probes. Each literal case has an explicit classification;
the six existing script-test generator macros have bounded name-prefix policies.
There is no default classification for an unknown source or new literal case.

## Running

```sh
make test/win32-debug TEST_LEVEL=unit
make test/win32-debug TEST_LEVEL=integration
make test/linux-debug TEST_LEVEL=unit
make test/linux-debug TEST_LEVEL=integration LABEL_FILTER='^no-render$'
make test/win32-debug TEST_LEVEL=unit DOMAIN=tactics_rts
```

`TEST_LEVEL=all` is the default and preserves the existing suite. Level, domain,
source labels and name-prefix filters intersect. An empty level selection is an
error. Existing `LABEL_FILTER='^unit$'` commands also work. Focused agent, terrain
stamp, tilemap editor and resource decoder projects use the same case catalogue;
select them with `ctest --test-dir <build> -L '^unit$' --no-tests=error`.
Aggregate profile/import registrations carry their source union, including
`mixed` where appropriate.

## Source impact index

Host builds automatically run `eve_test_impact_index` after compiling the normal
test targets and discovering their active CTest cases. The generated
`build/<profile>/test-impact/index.json` maps repository source/header paths to
case names, labels and dependency evidence. It is generated build data, never
another manually maintained module or coverage catalogue.

```sh
make test-impact/win32-debug TEST_BASE=origin/dev TEST_LEVEL=unit
make test-affected/win32-debug TEST_BASE=origin/dev TEST_LEVEL=unit
python3 -X utf8 scripts/test_impact.py select --build-dir build/win32-debug --changed src/modules/card/Card.cpp
```

The Make entrypoints build first. Direct Python selection refreshes the index;
direct `run` assumes the relevant binaries have already been rebuilt. `--changed`
may be repeated. Otherwise changes include committed differences from `--base`
(default `origin/dev`), staged/unstaged edits and nonignored untracked files.
`TEST_LEVEL` and optional `DOMAIN` intersect the affected set. The decision and
its reasons are written to `selection.json` and `affected-tests.txt` alongside
the index. Bundles, disabled registrations and opt-in benchmarks stay excluded.
For a multi-configuration generator, direct Python calls require `--config`
(for example `--config Release`); the Windows Release Make entrypoint supplies it.

Dependency facts come from the [CMake File API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html),
[CTest's JSON registrations](https://cmake.org/cmake/help/latest/manual/ctest.1.html),
and [Ninja compiler dependency records](https://ninja-build.org/manual.html).
Literal includes are also walked conservatively across conditional branches.
Test-source and private-header edits select their case-source labels; engine
implementation edits select the consumers of the actual transitive target/link
closure. A broad DLL or monolithic closure can legitimately select many tests.
This is impact analysis, not proof of behavioral coverage or a call graph.

Unknown paths, runtime assets, build scripts and external dependency changes
select all registered tests in the requested lane. Commands not tied to a
modeled executable remain conservative. Required normal test targets without
discovered cases cause an error, so an unbuilt domain cannot produce a false
empty result. The index is specific to its configuration/profile; explicitly
built optional composition targets appear only after discovery. Cross-compiled
device runners retain their existing test lane. On CMake 3.21–3.26, repeat the
first configure once to answer the File API query; CMake 3.27+ answers it during
the first configure. Full CI remains the authoritative cross-platform check.

## Consolidation policy

A module normally owns one C++ source under `test/unit/<module>/`. Only a suite exceeding
5000 lines is divided along named responsibilities; each resulting file stays
below that threshold. The module coverage audit enforces this rule. Integration
sources combine related cases by task goal; they are not module unit files.
Consolidated sections use private namespaces to isolate helpers. Source provenance
also drives compile selectors so standalone and trimmed builds retain their
existing responsibility selection instead of compiling every merged section.
Composition ownership remains independent of physical file count.

Human preview holds require `EVENGINE_TEST_MANUAL_PREVIEW=1`. Automatic tests
still render the frames needed by their assertions; no preview is required by
unit tests.

## Boundaries

Unit tests verify a local algorithm, owning value, codec, or gameplay rule with
explicit inputs and state. Small same-domain in-memory collaborators, injected
time/RNG and fake capability providers are acceptable. They do not initialize
rendering/audio, create windows, start the engine loop, load external assets,
access real files, start worker threads or wait on real clocks. Native C++ game
rules belong here; invoking their Squirrel bindings is integration coverage.

Integration tests verify actual module composition, script bindings, asset
pipelines, physics backends, OS services, runtime lifecycles and rendering.
Measured performance workloads are integration tests too. They can still be
CPU-only: a card/RTS payment transaction or script-binding contract does not
need a renderer. A geometry/tessellation test in the graphics domain can be unit.

Labels are independent dimensions:

- `unit` / `integration`: case boundary.
- `reason:<category>`: classification rationale. The category vocabulary and
  resource mapping are declared in `scripts/test_levels.py`.
- `no-render`: no graphics device or native window setup required by the case.
- `requires:graphics`, `requires:window`, `requires:filesystem`, `requires:audio`,
  `requires:network`, `requires:process`, `requires:threads`: external requirements.
- `source:<file>` and `unit_test_<domain>`: existing ownership/link-domain labels.

Resource labels are conservative, including supported branches. `no-render`
does not mean no filesystem, audio or other OS dependency. CPU ImGui context
tests use `reason:ui-render` without a GPU requirement. Profile aggregate labels
include all source branches; normal per-case discovery uses compiled identities.

## Source layout and ownership

- `unit/<manifest-module>/<responsibility>.cpp` owns local rules, values and codecs.
  Small cohesive suites are combined; complex modules keep meaningful boundaries
  such as abilities, board geometry, turn rules, state formats and resource models.
- `integration/<task>/<workflow>.cpp` owns a real composition goal, independently
  of any one module. Tasks include card/RTS payment, RTS unit lifecycle,
  save/restore, script APIs, rendering, physics and offscreen authoring admission.
- Standalone project entrypoints, probes, compile contracts and shared fixture
  implementations retain their dedicated directories/roles.

All formerly mixed desktop sources are physically separated. Literal case names
and assertions are preserved; bundles follow the new physical source files.
Default per-case process isolation and opt-in performance policy remain intact.
The catalogue's `module`, `task` and `origins` fields own attribution and migration
provenance. CMake profile/standalone references to an old name resolve to the
canonical source(s); an already canonical file is never expanded again. There
are no legacy forwarding `.cpp` files or a second manually maintained source list.

Unit registrations set `EVENGINE_TEST_NO_RENDER=1`. Desktop test main checks SDL
video state before and after execution; shared window/offscreen fixtures reject
that lane before creating a window/device. The source safety gate also checks
reachable helpers for rendering/VM/file/thread/wall-clock setup. Focused CPU
executables provide narrower link proofs; the main lane still links engine DLLs.
Device app runners retain their explicit SDL bootstrap and do not get a desktop
headless claim. The fuzz property retains its existing standalone entrypoint.

[MODULE_TEST_COVERAGE.md](MODULE_TEST_COVERAGE.md) records all declared modules,
owned units, task associations and header/API-name evidence separately. A reference
is not measured behavior coverage. Pure type-forwarding/link-anchor modules do not
get empty placeholder assertions. Unowned local-rule coverage stays visible in
the report even when composition tests reference the module.

## Maintaining coverage

```sh
python3 -X utf8 scripts/test_levels.py --summary
python3 -X utf8 scripts/test_module_coverage.py --write
make check
```

Add or update the source's case name under the appropriate reason group in
`test_levels.json`. Its `default` applies to declared generator prefixes and
source-only probes, never arbitrary new literal cases. Keep source roles accurate.
The schema is `evengine.test-levels`, version `1`; unknown fields and versions are
rejected rather than migrated implicitly. Removed/renamed sources or cases,
duplicate groups and stale generator policies fail the source-quality gate.

The scanner understands comments, raw strings, numeric digit separators,
fixtures, fuzz declarations and conditional implementations. Unit checks follow
reachable test-owned helper bodies and headers, rejecting obvious window/GPU/VM/
file/worker/wall-clock setup. This is a static backstop, not a C++ interpreter or
proof about indirect engine implementation. Review helpers and provider lifetimes
when changing a case, then run relevant cases. Runtime discovery rejects compiled
cases missing from the configured catalogue.

Classification changes reconfigure and refresh discovery. Unchanged metadata
does not rewrite its generated file. Case grouping during discovery avoids a
second all-cases scan for every source.

## Runtime and build scope

Desktop runners do not initialize rendering themselves. The main engine suite
retains its engine link closure and build prerequisites, so selecting units is a
runtime separation, not a graphics-free build claim. Focused CPU projects can
prove their narrower link closure separately. Android/iOS app runners still
perform SDL/audio bootstrap and keep their existing filters; this change does
not make those app runners headless.

See [TEST_LEVEL_AUDIT.md](TEST_LEVEL_AUDIT.md) for the baseline inventory and
verification scope. Counts include inactive platform/provider branches and
standalone sources; they are not a count of hosted tests that passed.
