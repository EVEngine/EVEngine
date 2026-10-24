# Testing Engine Changes

Sources: [Chinese testing conventions and coverage ledger](../../dev/测试覆盖.md), [contributor instructions](../../../AGENTS.md). [Developer index](README.md).

This guide translates testing conventions and the execution workflow. Per-module tables and historical counts remain in the Chinese ledger; they do not prove that every current API is covered.

## Source checks

Before opening a PR, run the source-only gate used by CI:

```sh
make check
make check CI_BASE=<base-commit>
```

It needs Python 3, Ruff, Pillow, clang-format 18 and git-clang-format. It checks dependencies, binding coverage, metadata, test registration, examples, critical returns, versions, profiles, architecture contracts, script lint and tests. It does not build the engine.

Generated signatures use the existing binding catalog extractor. Guides explain semantics without repeating every method name. Unresolved signatures and new bindings absent from both references fail strict coverage.

## Run affected tests

`BUILD_TESTING` is enabled by default. After building the host configuration:

```sh
make test/win32-debug
make test/linux-debug
make test/macosx-debug
make test/win32-debug FILTER=<prefix>
```

Normal CTest runs each case in its own process. Bundles are opt-in via `FILTER=bundle/<file>.cpp` or `ctest -L bundle`; window/GPU cases can behave differently and become much slower when sharing a process. The full platform suite belongs in CI; locally verify the changed domain and required linkage.

A successful compile does not prove rendering, packaging or consumer usability. Inspect real output for those changes.

## Write C++ tests

Use zeroerr `TEST_CASE`, `CHECK` and `REQUIRE`. Put cases in `test/<module>.cpp`, optionally split by subdomain, and register every source in `test/CMakeLists.txt`.

Names follow `Module.Subject.behavior`, for example `data.ByteData.roundTrip`. Coverage needs a normal successful path plus principal error/boundary paths. Merely reaching a method is partial coverage.

Prefer isolated fixtures. Construct DataModule and related types directly. `Filesystem::create()` is a process singleton already initialized: do not call `init()` again. Give each case its own identity/write directory and clean up its files.

Direct zeroerr regex filters should match whole names, for example `--testcase=^data\..*$`; a prefix-only expression may select no cases.

## Offscreen and windowed GPU tests

Prefer headless fixtures for parameters/resources/pipeline behavior and computed pixels:

```cpp
GfxFixture fixture(w, h, /*useHeadless=*/true);
```

Alternatively use `openHeadlessGfx()`. `Graphics::initHeadless()` creates a windowless Vulkan device; drawing uses an offscreen Canvas and `newImageData()` readback. `present()` is a no-op. See `test/integration/render-pipeline/render_pipeline_graphics_graphics_backend_parity_tests.cpp`, `test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp` and `test/integration/render-presentation/render_presentation_graphics_RenderSystem3D_tests.cpp`.

Initialization is idempotent for the shared Graphics singleton: later calls update the logical viewport rather than creating a second device. Headless GPU tests still need Vulkan.

Keep windows for interactive/visual demonstrations, ClassicScenes, render-image audits and swapchain readback (`getPixel`, `setScreenReadbackEnabled`, `saveFramePng`). Linux containers running these paths need a display server such as Xvfb. With software Vulkan and null audio:

```sh
export VK_ICD_FILENAMES=/path/to/lvp_icd.json
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export ALSOFT_DRIVERS=null
xvfb-run -a make test/linux-debug FILTER=<prefix>
```

Use the actual installed ICD path; `AGENTS.md` lists the prepared CI image's path. Capture visual evidence with the engine screenshot path; Xvfb's framebuffer is not reliable evidence of engine rendering.

## Maintain coverage evidence

The Chinese matrix covers public C++ header APIs. `.nut` smoke tests remain useful but do not count toward this native matrix. Cover meaningful ownership/clone/lifetime behavior and abstract interfaces rather than duplicating every backend.

- Covered: normal behavior and principal failure/boundary behavior are tested.
- Partial: a case reaches the API without establishing its full contract.
- Untested: no matching case exists.
- N/A: unimplemented, no independent test point or a platform stub; record why.

Add entries/tests with new public APIs where practical. Record bugs and gaps explicitly. Mark unavailable APIs N/A with a reason rather than silently deleting them. Update case names and status when tests land.
