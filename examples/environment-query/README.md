# Environment query

Run with `make run/linux-debug GAME=examples/environment-query`.

The sensing query generates 24 world-space ring positions around the red target,
ranks them by distance from the blue unit, and returns owning location candidates.
The example consumer explicitly chooses the first candidate and moves toward it.
Green intensity shows utility. This geometric example does not claim navigation
or obstacle avoidance; LOS is disabled explicitly because it needs a provider.

`world.debugLastQueryJson()` and `eve_sensing_query` expose the versioned
`environment` trace: generation, test counts, metric bounds and contributions.
C++ callers can retain `EnvironmentQueryPlan` between requests, supply named
contexts and custom metrics, and request `Best` for safe deferred validation.
