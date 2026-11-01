# HD2D native house integration plan

1. Reuse HouseGen request/layout and its facade selection. Keep the marked map lot as a placement constraint, not a second collision mask.
2. Replace offline Python/OBJ generation with an EveScript presentation recipe consuming HouseLayout instances and Procgen prototype recipes. Native recipes own triangles/normals/UVs; scripts own style and composition. Merge CPU meshes by material before upload.
3. Use the native gable/flat/shed roof contract. Do not mislabel hip as gable or add an unrelated layout format. Support four facades, bounded per-cell door/window details, world-scale UVs, separated roof courses and facade-only posts.
4. Validate candidate layout, occupancy and meshes before publication. Uploaded GPU meshes are Graphics-owned; renderables are retained in scene arrays; build only at initialization, switch prebuilt variants by visibility. No callbacks during generation, no new ECS systems or cross-domain links.
5. Run native CPU composition fixtures and scene checks, smoke, engine screenshots and make check. Retain truthful limits (SDK vs source build and no cross-platform verification).

No new public C++ API, persistent format, module or dependency. Existing HouseLayout JSON and component-library format remain canonical. Parameters are source-authored .nut values; layout seed and named roof-color seed are separate. Gameplay time remains the existing fixed step. No Python house geometry or renderer fallback.

Implementation status (2026-10-26): steps 1-4 complete. Native SDK scene check passed (71 assertions);
mesh/layout fixtures passed (178148 assertions across four presets). Occupied-footprint switching was
rejected in a focused runtime check. The example survived the six-second smoke window after removal
of the OBJ/Python assets. Four screenshots were captured by eve_screenshot following real key events.
No public API, module boundary or persistent-format change; no architecture exception requested.
Source-quality gate is run separately; no fresh engine build or hosted CI result is claimed.

Source-quality outcome: make check passed format, layering, bindings, examples, architecture and lint;
its 531 script tests initially reported two compiler-environment failures (five skipped). With the
repository MSVC wrapper, all nine Dawn/SPIR-V tests passed. The seven SDK architecture tests still
have one failure (one skipped): the macOS install fixture interpolates a Windows dependency path
into CMake and rejects the backslash-U escape. This is outside the changed example. Full make check
is therefore not green; the nodiscard compiler probe was also skipped in the initial environment.
