# Worker 3: bugs found during unit-test rewrite

No implementation bug has been confirmed yet.

## Test defect: legacy DayNight derived-state assertions skipped `update`

- **Input:** Construct `DayNight`, call `setTimeOfDay(12)` or `setTimeOfDay(0)`, then read `getSunElevation()` / `isNight()` without calling `update(dt, gfx)`.
- **Expected by old test:** The derived solar elevation and night flag immediately track the new hour.
- **Actual code path:** `setTimeOfDay` stores the wrapped hour; the derived sun fields are recomputed by `update`. The old tests therefore asserted a behavior the setter does not implement.
- **Sources:** `test/unit/daynight/daynight_tests.cpp` (prior `sunElevationPeaksAtNoon` / `nightBelowHorizon` cases); `src/modules/daynight/DayNight.cpp` (`setTimeOfDay`, `update`).
- **Reproduction status:** Confirmed by source inspection; runtime execution pending. The rewritten setter tests assert the documented storage behavior and leave renderer-derived state to an update-path test.
