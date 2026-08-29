# AGENTS.md

EVEngine is a C++20, Vulkan-based game engine (SDL2 + Squirrel scripting). Standard
build/test/run commands live in [`Readme.en.md`](Readme.en.md) and the root
[`Makefile`](Makefile) — use those; the notes below cover the non-obvious platform
caveats. The Cursor Cloud VM is Linux; on a Windows host use the Windows section.

> CI 问题调试：本地复现 / WSL2 / SSH 到 Mac mini 的完整手册见本机
> `.local-debug/CI-DEBUG-PLAYBOOK.md`（含私有机器信息，**勿提交到 GitHub**）。

## Cursor Cloud specific instructions

On this Linux VM the host target is `build/linux-debug`.

### Environment is prepared by the startup update script
The update script installs all system dependencies (build tools, X11/Wayland dev libs,
audio, `libvulkan-dev` + `glslc`/`glslang-tools`, Mesa Lavapipe software Vulkan,
`vulkan-tools`, `xvfb`), initializes the `external/*` git submodules, and pins the
default C/C++ compiler to GCC. You normally do not need to install anything.

### Compiler: use GCC (do not switch to clang)
CI builds Linux with GCC, and the update script points the `cc`/`c++` alternatives at
`gcc`/`g++`. The image's out-of-the-box default `cc`/`c++` is clang-18, which selects the
GCC-14 tree and then fails to link (`cannot find -lstdc++`) because only `libgcc-14-dev`
ships by default. Keep GCC as the default; the `Makefile` invokes plain `cmake` with no
compiler override.

### Building
- `make debug` configures + builds `build/linux-debug` (equivalently: cmake configure,
  then `--target deps`, then the engine).
- The first configure downloads the `third-party/` sources from GitHub and the `deps`
  target compiles them (SDL2, Poco, OpenAL, Box2D/box3d, FreeType, …). This is slow the
  first time and cached afterward. `build/` is git-ignored and tied to its source tree.
- Memory: keep parallelism modest (e.g. `make debug JOBS=4`) — this VM has ~4 cores.

### Running the engine and tests is headless — needs software Vulkan + Xvfb + null audio
`eve` and the graphics unit tests create a real `SDL_WINDOW_VULKAN` surface, so they need
a display and a Vulkan ICD. This VM has no GPU and no audio device. Export these before
running anything that opens a window (mirrors CI):

```sh
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json   # Mesa Lavapipe (llvmpipe) software Vulkan
export XDG_RUNTIME_DIR=/tmp/xdg-runtime && mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
export ALSOFT_DRIVERS=null                                     # OpenAL Soft null backend (no audio card)
```

- Unit tests (full suite, ~1500 per-case tests, a couple of minutes):
  `VK_ICD_FILENAMES=... ALSOFT_DRIVERS=... xvfb-run -a make test/linux-debug`
- `make test/*` runs tests per case (process-isolated; this is the fast path on
  CI) and injects fast headless defaults for ClassicScenes
  (`VIEW_SECONDS=0.3`, `PERF_FRAMES=30`). "bundle/<file>" CTest entries exist
  as an opt-in (`make test FILTER=bundle/<file>.cpp` or `ctest -L bundle`) —
  do not make bundles the default: GPU/window tests were ~70x slower when
  several shared one process on CI. Restore interactive view times with
  `make test VIEW_SECONDS=4 PERF_FRAMES=120`. See
  `docs/dev/superpowers/specs/2026-08-18-test-suite-optimization.md`.
- Run an example (long-lived GUI loop — use `timeout` for a smoke run):
  `cd examples/basic && VK_ICD_FILENAMES=... ALSOFT_DRIVERS=... XDG_RUNTIME_DIR=... xvfb-run -a ../../build/linux-debug/src/engine/eve run`
- `scripts/smoke_examples.sh <name>` launches an example headless and greps for error
  markers (PASS/FAIL) — a good quick check under `xvfb-run`.
- The benign `[ALSOFT] Failed to set real-time priority` warning is expected on this VM.

### Capturing a rendered frame (visual artifact)
Do not capture Xvfb's framebuffer or use it as visual-correctness evidence; that
path is unreliable. Start `eve`, connect to the engine's built-in MCP endpoint,
and use the engine-owned screenshot function. Xvfb may only provide an SDL
display for automated non-visual tests; it is not a screenshot backend.

## Windows (native) instructions

On a Windows host the same root `Makefile` drives MSVC builds. There is no startup
update script here — configure the environment once, then build with the Makefile.

### One-time environment setup
Required:
- **Visual Studio 2026** with the "Desktop development with C++" workload
  (or 2022 — set `VS_GENERATOR="Visual Studio 17 2022"` in the Makefile). The
  debug build locates `vcvars64.bat` via vswhere (`cmake\with-msvc.cmd`), so the
  workload must include the MSVC x64/x86 tools component.
- **CMake ≥ 3.21** and **Ninja** on `PATH` (the debug build uses the Ninja generator;
  release uses the Visual Studio generator).
- **Git for Windows** on `PATH`. The first configure downloads the `third-party/`
  sources from GitHub and the `deps` target compiles them (SDL2, Poco, OpenAL,
  Box2D/box3d, FreeType, …) — network access is required the first time.
- **Vulkan SDK** (LunarG): install, then set the user environment variable
  `VULKAN_SDK` (e.g. `C:\VulkanSDK\1.4.357.0`) and add `%VULKAN_SDK%\Bin` to `PATH`
  (provides `glslc`). **Open a new terminal / restart the IDE afterwards.**
- Optional but recommended: `choco install doxygen` for `make docs`.

Initialize the git submodules (the Linux VM's update script does this
automatically; on Windows it is a manual step):

```powershell
git submodule update --init --recursive
```
(equivalently `make init/submodules`). `third-party/` itself is not a
submodule: the first cmake configure clones it at the pinned commit
(`EVENGINE_THIRD_PARTY_PIN` in `CMakeLists.txt`) and verifies any existing
checkout is on that commit.

### Building
- Debug (Ninja + MSVC `cl`, fastest local iteration):
  `make build/win32-debug`
- Release (Visual Studio generator):
  `make build/win32`
- Do not invoke `cl.exe` manually outside a Developer prompt; the `cmake\with-msvc.cmd`
  wrapper calls vcvars64 before CMake so the MSVC compiler and STL are found.
- First build compiles third-party through the `deps` target — slow once, cached
  afterward. Keep parallelism modest on small machines:
  `make build/win32-debug JOBS=8`.
- The root `CMakeLists.txt` adds MSVC-specific flags automatically: `/utf-8`
  (Chinese Windows code page 936 avoids C4819 on UTF-8 sources), `/EHsc`
  (exceptions, required by zeroerr ASSERT), `/FS` and `/bigobj` (large TUs such as
  `Graphics.cpp` exceed the default COFF section limit under `/GL`).

### Running and testing
Windows has a real GPU and audio device, so the Linux software-Vulkan/Xvfb/null-audio
setup is **not** needed:
- Run an example: `make run/win32-debug GAME=examples/basic`
- Unit tests (zeroerr suite via CTest): `make test/win32-debug`
  (Release: `make test/win32`)
- Filter by test-name prefix: `make test/win32-debug FILTER=math.*`

### Docs and assertions
- API docs: `make docs` → `docs/api/html/` (requires doxygen on `PATH`).
- zeroerr ASSERT checks (`EV_PARAM_CHECK` / `EV_ASSERT` in
  `src/engine/common/Assert.h`): enabled by default in Debug, compiled out in
  Release unless explicitly enabled:
  `make build/win32 CMAKE_EXTRA_ARGS=-DEVENGINE_ENABLE_ASSERTS=ON`

### WSL2 alternative
To use the Linux toolchain against the same tree (e.g. for headless CI parity or a
Linux-only dependency), install WSL2 + the Linux packages from
[`Readme.en.md`](Readme.en.md), then:

```sh
make wsl/linux-debug
```

## Code conventions (API documentation)
- Public APIs in headers must use Doxygen comments: Javadoc style
  `/** @brief ... @param ... @return ... */` (or `/**\n * @brief ...\n */` blocks).
  Plain `//` or `/* */` comments are for implementation notes only — Doxygen's
  `make docs` output is built from the `@brief`/`@param`/`@return`/`@throws`
  commands, so undocumented public functions show up as empty entries.
- Parameter validation and internal invariants use `EV_PARAM_CHECK` / `EV_ASSERT`
  from `src/engine/common/Assert.h` (zeroerr-backed): enabled in Debug, compiled
  out in Release unless the build is configured with `-DEVENGINE_ENABLE_ASSERTS=ON`.

## Debugging playbook

These rules come from hard-won experience on this repo (Vulkan/GPU work on a
hybrid-GPU Windows host). Follow them before blaming hardware, drivers, or SDK
versions.

### Principles (user-mandated)
- **Search first, guess second.** When stuck, look up the actual API contract and
  constraints (official Vulkan docs, Stack Overflow, vendor release notes) before
  concluding "driver bug", "GPU poisoned", or "SDK version issue". Unverified
  device/driver theories are almost always wrong.
- **Enable the validation layer early.** `EVENGINE_VULKAN_VALIDATION=1`
  (any value except "0") turns on Khronos validation at instance creation; it
  prints VUIDs that name the exact rule being violated. It is 5-20x slower, so
  use it for short targeted test runs only.
- **Reduce to a minimal reproduction first.** Build the smallest test that
  exercises the suspected code path before touching production code paths.
- **Rule out upstream layers before the GPU.** If SDL/third-party/init code is in
  the path, prove whether it is the cause with a tiny probe before spending time
  on driver diagnostics.
- **Empirically verify driver behavior.** Never assert "AMD does X" from memory;
  check the feature bits, the validation output, and at least one other device
  (see `EVENGINE_GPU_DEVICE=integrated|discrete|first`) before changing driver-
  specific branches.

### Repo-specific crash backtrace
- The engine installs a Windows unhandled-exception filter that prints a
  symbolized stack trace: `src/engine/common/CrashHandler.h` →
  `eve::installCrashHandler()`. It is wired into BOTH `src/engine/main.cpp`
  (the `eve` binary) and `test/main.cpp` (unit_test; `test/CMakeLists.txt`
  links `EVBacktrace` on WIN32).
- If a test crashes and prints nothing but a bare exit code (e.g.
  `0xC0000005` = access violation), the handler is not active in that binary.
  Verify with `rg -a "\[crash\] code=" <binary>`.
- `eve` supports `EVE_TEST_CRASH=1` to force an access violation right after
  startup so the handler output can be verified.

### Reading a backward-cpp stack
- backward prints "most recent call last": frame #N+1 called #N.
- Frames below `KiUserExceptionDispatcher` (`RtlLocateExtendedFeature`,
  `_chkstk`, `_C_specific_handler`, `strncpy`, `UnhandledExceptionFilter`) are
  exception-dispatch machinery, not the cause. The frame directly ABOVE
  `KiUserExceptionDispatcher` is the crash site.
- An AV whose exception address is inside the module usually means a bad memory
  READ (e.g. dereferencing a NULL/garbage struct member); a low address like
  `0x0` means call-through-NULL.

### Isolation patterns that work here
- **Probe test**: a self-contained TEST_CASE that calls the underlying API
  directly (SDL window + Vulkan surface creation, or a minimal `vkCmdDraw`
  through the exact pipeline) and ignores the engine state. If the probe passes
  but the engine path crashes, the bug is in engine state, not the library.
- **Check what a merged feature actually did**: a commit can be in the branch
  (`git merge-base --is-ancestor`) yet not affect the binary you run (e.g. the
  backtrace handler existed for `eve` but not `unit_test`). Verify the produced
  artifact, not just the git history.
- **Include-path tracing**: MSVC emits `注意: 包含文件:` (/showIncludes) lines —
  grep the build log for the header in question to see which copy was picked.
- **SDL gotcha**: `<SDL2/SDL.h>` does NOT include `SDL_vulkan.h`; include it
  explicitly for Vulkan entry points, or you get C3861 "identifier not found".

### When behavior regresses mysteriously
- **Stale objects are a real failure mode here.** The Ninja targets use
  "unscanned" dependency tracking (`CXX_COMPILER__*_unscanned_Debug`), so a
  HEADER edit does NOT reliably trigger recompiles of every TU that includes
  it. After changing a widely-included header (e.g. `vulkan/Graphics.h`), a
  deterministic nonsense crash (garbage member values, "Invalid device" from a
  long-stable call) often means some TU still encodes the old class layout.
  `--target clean` is not enough here: delete the build dir
  (`Remove-Item build/win32-debug -Recurse -Force`) and reconfigure with
  `-DEVENGINE_THIRD_PARTY_BINARY_DIR=<prebuilt deps>` plus the download options
  OFF (assets are pre-seeded), then rebuild, before deep-diving. To make header
  edits visible again, touch every TU that includes the header (or switch those
  targets off unscanned mode).
- The build reuses a read-only prebuilt third-party install
  (`C:/Users/xiaofans/Workspace/Agents/EVEngine/build/third-party-binary/
  win32-debug`). Check its lib timestamps (`Get-Item ... | Select LastWriteTime`)
  when third-party behavior unexpectedly changes.
- A deterministic crash inside an SDL call that works in a probe points at
  engine-side state (e.g. `SDL_InitSubSystem`/`SDL_QuitSubSystem` pairing, the
  SDL global `_this`, or window flags) — instrument the state before touching
  the SDL/driver layer.

## Collaboration conventions (multi-agent parallel work)
## Collaboration conventions (multi-agent parallel work)

These rules exist to keep several agents working in the same tree without
stepping on each other. `docs/dev/模块编排与裁剪架构.md` explains the layering
model behind them.

- **Cross-module calls go through interfaces, not includes.** When a lower
  module must reach a higher one (e.g. `filesystem` reaching `graphics`), the
  consumer declares an interface (in `src/engine/common/` or its own module),
  the provider registers it via `eve::cap::provide/query`
  (`src/engine/common/Capability.h`). Do not add a new upward `#include` —
  `scripts/module_depgraph.py --check` (CI `layering` job) fails on it.
- **One manifest, one boot list.** New modules are declared only in
  `cmake/module_manifest.cmake`; the link list, third-party closure and
  `eve.moduleList` are derived. Do not hand-edit `EVELIBS` / `ThirdParty` /
  `load.nut` module wiring.
- **Keep public headers free of cross-module includes.** Prefer forward
  declarations and Pimpl so a low-level type change does not recompile every
  dependent module. Check `python3 scripts/module_depgraph.py` for `*` marks
  (leaks into a module's own headers).
- **Big files get split, not extended.** A `.cpp` over ~1000 lines is a
  single-agent-at-a-time file. Split it along existing section comments into
  multiple TUs (pure moves, no behavior change) instead of appending more
  methods.
- **Formatting is enforced on changed lines of existing files.** CI runs
  `.github/scripts/check-format.sh` (clang-format-18, `.clang-format`) with
  `git clang-format`; pre-existing debt in untouched regions does not block a
  PR, and brand-new files are skipped with a warning. Before committing, run
  `git clang-format` (formats only your changed lines) and format new files by
  hand; never reformat whole files unrelated to your change.
- **Tests stay per-module.** New tests go into their own file under `test/`
  (zeroerr cases; each case is process-isolated via CTest). Do not grow a
  shared test main.
- **PR granularity.** An interface change ships as one PR that updates the
  interface, every backend and every consumer — no intermediate commits that
  break CI. Use `codex/` branch prefixes for agent work.

## Mandatory architecture rules for refactoring agents

Before changing a public API, ECS model, persistent format, module boundary, or
cross-domain lifecycle, read and follow:

- `docs/dev/重构代码质量与系统完整性规范.md`
- `docs/dev/领域短根继承与跨域组合架构.md`
- `docs/dev/Result检查与不得丢弃返回值规范.md`
- `docs/dev/2026-08-26-architecture-consolidation-checklist.md`

These are requirements, not optional design suggestions:

- Do not add ambiguous operation-result `bool`, `nullptr/false + lastError`, or
  discardable critical return values. Use structured `Result`, strong status
  enums, and `[[nodiscard]]` according to the Result specification.
- Domain types use short inheritance roots. Orthogonal state/capability uses
  components; cross-domain relationships use typed Link/Handle. Do not create a
  universal GameObject, GameplaySubject, or GameplayActor root.
- Each mutable fact has one authoritative owner. For every Link define ownership,
  stale detection, both destruction orders, and restore/hot-reload rebuilding.
- Before implementing an ECS System, record its entity/type scope, View,
  read/write component sets, structural mutations, events, services, and phase.
  Use deferred structural mutation while iterating views.
- Inject simulation time, dt, and named RNG streams. State the determinism or
  tolerance contract for replay, serialization, networking, and backend parity.
- Document public API ownership/lifetime, thread affinity, and callback reentrancy.
  Never retain temporary raw pointers across frames/tasks or invoke unknown
  callbacks/scripts while holding a lock.
- Persistent/cross-process data requires schema id, version, unknown-field policy,
  and migration. Restore/import must not leave partially mutated observable state.
- An optional dependency is only optional after tests cover both provider-present
  and provider-absent configurations. Fallbacks must be explicit and observable.
- Interface implementations share contract tests. Cross-module refactors include
  a composition test and relevant trimmed-build profile, plus failure injection
  for important lifecycle and transaction paths.
- New TODO/HACK/FALLBACK/ALLOWLIST/soft-skip entries require an issue, owner,
  reason, and removal condition or expiry. Do not create a second source of truth
  as a compatibility shortcut.

In the final handoff, report which of these rules applied, what was verified, and
any deliberate exception. An exception requires explicit user approval; an agent
must not silently waive these requirements.

### Mandatory top-level architecture gate

Before handing off a refactor, run the source-only contract gate and its fixtures:

```sh
ARCHITECTURE_BASE=HEAD make check/architecture-contracts
```

CI supplies the pull-request base SHA. `scripts/architecture_contracts.json` is the
single catalogue for contract evidence; do not silence a finding with a new baseline,
allowlist, or broad scope. A compatibility facade may retain a legacy shape only when
the public documentation states that it is compatibility-only and the canonical
Result/status API is the inward implementation. New debt markers still require the
owner/issue/reason/expiry/removal metadata enforced by `check/quality-metadata`.
