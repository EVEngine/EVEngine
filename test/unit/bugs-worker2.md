# Worker 2: bugs found during unit test rewrites

Findings below record the triggering input, expected and actual behavior, owning source location, and reproduction status.

Executable verification is pending while the shared Windows build runs. Source inventory checks have been run; transient errors in other workers' files appeared while they were being replaced.

## SensingWorld::remove clears query cache on a missing id

- Input: Upsert `subject`, run `circle(...)` so `resultAt(0)` returns it, then call `remove("missing")`.
- Expected: `remove` returns `NotFound` and leaves the last-query cache intact. The `resultAt` lifetime contract in `src/modules/sensing/Sensing.h` names only a subsequent query/circle/box, restore, or world destruction as invalidators.
- Actual: `SensingWorld::remove` calls `results_.clear()` before checking whether the id exists, then returns `NotFound`; `resultAt(0)` becomes empty.
- Source: `src/modules/sensing/Sensing.cpp`, `SensingWorld::remove`; `src/modules/sensing/Sensing.h`, `resultAt` lifetime documentation.
- Reproduction status: Confirmed by direct source control-flow inspection on 2026-10-25; compiled reproduction remains pending. The mismatch also occurs for a successful removal, because removal is absent from the documented invalidation list.
