# HUD toolkit implementation

Scope: extend retained UI for screen HUDs. UIHost owns layout and presentation;
gameplay remains authoritative for HP/cooldowns. No new ECS System or gameplay
root. Existing world anchors and backend-owned texture registrations retain their
ownership. New script factories compose existing nodes and emit existing UI events.

## First delivery

- Fit design canvas, per-host user scale and caller-provided safe margins.
- Display-only host input transparency; separate interactive hosts.
- Linear image fills (crop/stretch, four directions), solid radial sectors/rings.
- Resource bar and skill slot factories, C++ and EveScript.
- Version 3 document fill properties, v1/v2 migration, default-font text styles.
- Explicit unsupported-content diagnostics and atomic runtime publication.
- HUD gallery, geometry/layout/publication regression coverage.

Implementation uses existing ImGui draw primitives and the ordered texture bridge.
No new module dependency or renderer interface is introduced. The radial overlay
is deliberately a solid primitive; textured radial masks need backend triangle
support and are a later extension. Existing pixel-mode hosts are opt-out by default.
A reusable UI-thread scratch buffer snapshots logical metrics during measure/walk;
no application callbacks run during these passes. Runtime value setters remain the
existing ID lookup API; generation-safe node update handles are not added here.

## Following deliveries

- Typed node handles with tree-generation invalidation and hot-reload tests.
- Value tweening, transient state animation policies, status lists and message queues.
- Full content resource resolution (font/texture), authored canvas settings, and
  shared runtime/editor layout for preview parity. Current preview planner does
  not yet render the new fill primitives; do not claim P3 completion.
- Platform safe-area adapter, width/height match scaling, mixed-host empty-area input
  pass-through; full world-anchor scaling verification and large-crowd benchmark.
- Authoring tools: canvas guides, snapping, undoable drag/resize, template assets,
  language/viewport preview presets.

## Verification

Geometry covers reverse UVs, bounds, invalid fractions, sweep direction and rings.
UI tests exercise component reconciliation and scaled physical rectangles while
persistent logical inputs remain unchanged. Document tests cover roundtrip,
validation and failed publish preserving the live tree. Real engine screenshots
must use the engine MCP endpoint, not Xvfb framebuffer. Validate relevant tests and
`make check`; report any environment or pre-existing build failures explicitly.


## Verified first delivery

- Linux GCC Debug minimal profile with font/devtools/ui_editing enabled: host built.
- Targeted ZeroErr harness using the repository test runner and the new UI/document
  cases: 10 cases, 140 assertions passed, no failures or skips.
- `make check CI_BASE=HEAD`: passed; repository script suite ran 313 tests.
  After the final display fixes, format/architecture/example/test-manifest checks
  and the HUD cases were run again.
- Native Vulkan software rendering, X11 system input, engine MCP observation and
  engine-owned PNG capture: 1280x720, 1280x800, 1680x720, scale 1.25, safe margins,
  scene/decorative input pass-through, physical skill click and cooldown verified.
- Pixel checks confirmed resource and skill textures after every window recreation.
  The example re-registers textures and remounts HUDs because the existing
  Window.setSize path recreates the UI backend. Opaque registration IDs remain
  bounded by that backend's lifetime.
- Two actual main.nut reloads retained HP and reconstructed the HUD. Captures and
  runtime receipts are in build/hud-artifacts (not source-controlled).
- Display regressions fixed: fit measurement must not mark ImGui's fallback window
  as written; translucent radial triangles must not add internal AA fringes;
  scaled nine-patch destination borders must preserve source texel coverage.

The existing UIHost ownership, Result observation, deferred event routing and
versioned document rules were retained. No deliberate architectural exception.
Full Windows/WebGPU builds and P2/P3 authoring work were not part of this delivery.

## Follow-up: node identity and transient values

- Native `UINodeHandle` combines the owning ECS handle, tree generation and node index. Property reconciliation preserves it; replacement and host destruction invalidate it. Resolve on the UI thread and retain tokens rather than borrowed node references.
- All item tweens resolve tokens in constant time and cancel stale targets, including during delays. A replacement tree never receives an old animation merely because ids match.
- `animateItemValue` adds normalized, clamped value transitions with retargeting, delay and immediate completion. The gallery uses it for the resource trail, while gameplay retains authoritative HP.
- Document publishing accepts a borrowed texture resolver, caches distinct keys for one publication and stages all descriptions before changing the live host. Callers own backend registrations and must republish after backend recreation.
- Still pending: script-facing typed tokens, font resolution, image fitting, status/message components, canvas authoring and shared editor/runtime preview layout.

Follow-up validation: the Linux minimal host with font, devtools and ui_editing builds successfully. The focused native harness runs 25 HUD/document/tween cases with 277 assertions, all passing. The engine gallery passes physical scene/skill input, three resolutions, scale/safe-area changes and normal hot reload. A live value sample confirms the trail initially remains at 1.0 after HP falls to 0.85, then converges to 0.85. Engine readback: `build/hud-artifacts/hud-value-trail.png`.

The complete `make check CI_BASE=HEAD` source-quality gate also passes, including 313 repository script tests. Windows and WebGPU compilation remain outside this Linux verification.
