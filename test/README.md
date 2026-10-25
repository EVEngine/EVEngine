# C++ test levels

The source policy is [test_levels.json](test_levels.json). It covers every
C++ source under `test/`, including standalone projects, profile checks, fuzzing,
helpers and manual probes. Literal case names come from C++ declarations; file
defaults classify them unless an explicit case override applies.
The six existing script-test generator macros have bounded name-prefix policies.
Unknown sources still require an explicit file policy. The full per-case catalogue
and CTest labels are generated in the build directory, not maintained in Git.

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
Consolidated unit sections use private namespaces to isolate helpers. Integration
workflows use physically merged `.cpp` sources and do not use origin-selection
macros. The intermediate mechanical split into hundreds of scenario files has
been reverted. Related scenarios share one real translation unit; this does not
rely on CMake Unity or generated wrapper translation units. Helpers keep private
namespaces so consolidation preserves ownership and avoids name collisions.
A focused harness may own a genuinely separate contract source when linking the
aggregate would introduce unrelated runtime dependencies. Existing unit-only
legacy selectors remain outside this integration reorganization.

The [coverage audit](../docs/dev/testing/integration-audit.md) lists every original
case, its current location, named subcase or explicit retirement reason. The
[optimization notes](../docs/dev/testing/integration-optimization.md) explain
boundary decisions, fixture sharing, mocks and verification limits. Regenerate
the audit with `python -X utf8 scripts/audit_integration_tests.py`.

Timing and long-running stability workloads under `benchmark/` are excluded from
the normal build. Routine integration cases use short, bounded simulations with
small time steps; long matches, endurance loops and frame-count exhaustion
regressions remain explicit benchmark workloads.
To build and run them explicitly:

```sh
make test/win32-debug CMAKE_EXTRA_ARGS=-DEVENGINE_BUILD_TEST_BENCHMARKS=ON INCLUDE_BENCHMARKS=1 LABEL_FILTER='^benchmark$'
```

On Windows, `make test/win32-debug` and `make test/win32` run CTest on a private
background desktop by default. SDL windows remain shown and rendering normally on
that desktop, including visibility and presentation assertions; the user's active
desktop is never switched. The launcher verifies child desktop placement before
starting tests and fails explicitly if isolation is unavailable. Use
`TEST_WINDOWS=visible` for interactive debugging. Direct `ctest` calls bypass this
default; wrap them with `python3 scripts/run_ctest.py -- ctest ...` to isolate them.
Closing the launcher also terminates its remaining test process tree.

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
Timing workloads use the explicit benchmark build/run lane; their boundary labels
still describe the exercised contracts. Integration tests can be CPU-only:
a card/RTS payment transaction or script-binding contract does not
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

Integration workflows are physically consolidated by task. Literal case names,
assertions and named subcases remain traceable in the audit; local-only cases
stay in their owning unit files. Default per-case process isolation and opt-in
benchmark policy remain intact. Canonical source paths own unit modules and
integration tasks. Catalogue provenance may resolve a former scenario basename
to its owning aggregate for existing profile/standalone callers; it is not a
second compilation source or a preprocessor selection mechanism. There are no
legacy forwarding `.cpp` files, generated Unity wrappers, or manually duplicated
live test source lists.

Unit registrations set `EVENGINE_TEST_NO_RENDER=1`. Desktop test main checks SDL
video state before and after execution; shared window/offscreen fixtures reject
that lane before creating a window/device. The source safety gate also checks
reachable helpers for rendering/VM/file/thread/wall-clock setup. Focused CPU
executables provide narrower link proofs; the main lane still links engine DLLs.
Device app runners retain their explicit SDL bootstrap and do not get a desktop
headless claim. The fuzz property retains its existing standalone entrypoint.

The on-demand module coverage report records all declared modules,
owned units, task associations and header/API-name evidence separately. A reference
is not measured behavior coverage. Pure type-forwarding/link-anchor modules do not
get empty placeholder assertions. Unowned local-rule coverage stays visible in
the report even when composition tests reference the module.

The report is generated build data and is not committed. Source-quality checks
audit coverage directly without comparing a checked-in snapshot.

## Maintaining coverage

```sh
python3 -X utf8 scripts/test_levels.py --summary
python3 -X utf8 scripts/test_module_coverage.py --output build/reports/module-test-coverage.md
make check
```

Add a source policy with its role and `default` classification. Unit module
ownership comes from `unit/<module>/`; integration task ownership comes from
`integration/<task>/`. Declare integration module ownership explicitly.
New or renamed literal cases inherit that default automatically. Only list cases
under `cases` when their classification differs from the default, for example:

```json
{
  "role": "tests",
  "default": "module-composition",
  "module": "example",
  "cases": {"script-binding": ["example.scriptContract"]}
}
```

Omit `cases` when there are no exceptions. Unit safety checks still inspect every
inherited case, including newly added cases and reachable helpers. Review each
new case's resource needs, especially in integration files with a CPU default.
Keep roles and explicit bounded generator prefixes accurate.

The schema is `evengine.test-levels`, version `2`. Version `1` remains readable
for migration and existing tool fixtures; new policies use version `2`. Unknown
fields and versions, removed sources, stale overrides, duplicate groups and stale
generator policies fail the source-quality gate.

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
