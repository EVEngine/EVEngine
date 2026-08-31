# PixelWorld Physics

`pixelworld_physics` is an optional L5 composition module. It converts an owning
`PixelFragment` detached by PixelWorld into one Box2D dynamic body without making
Physics a second source of live terrain.

`PixelFragmentBody::create()` performs deterministic top-left greedy rectangle
decomposition, creating one fixture per maximal rectangle under an explicit fixture
budget. The adapter retains the detached bitmap and a generation-qualified
`PhysicsLink`; it never retains `World*` or `Body*` across calls.

`settleIfSleeping()` resolves the link, leaves awake bodies unchanged, and snaps a
sleeping bitmap to the nearest quarter turn. It first asks PixelWorld to rasterize the
entire bitmap transactionally and destroys the physics body only after that succeeds.
Occupied terrain, stale PixelWorld epochs, or stale physics handles therefore leave
the body/bitmap ownership intact. `releasePhysics()` explicitly destroys a live body
without returning its bitmap to terrain.

`PixelTerrainCollisionCache` is the static projection. `sync()` consumes only owning
dirty-Chunk snapshots newer than its represented revision and stages every replacement
body before changing the live cache. Static terrain uses deterministic binary boundary
contours with collinear simplification and Box2D chain/loop fixtures, rather than filled
per-pixel rectangles. Extraction samples a one-cell authoritative neighbor halo, suppresses
internal Chunk seams, and rebuilds an already-projected cardinal neighbor when a dirty border
can invalidate its contour. Every Chunk has explicit contour/vertex budgets. A failed candidate
destroys all staged bodies and preserves existing cache entries; PixelWorld restore, Catalog
epoch change, or a different Physics world forces a full rebuild without dereferencing stale
pointers.

Character controllers can call `probeTerrainCircle()` for current penetration/material contact
and `sweepTerrainCircle()` for continuous movement. Both query PixelWorld directly, ignore
non-solid materials, enforce candidate-cell budgets and use deterministic cell tie-breaking.
Sweep tests straight faces and rounded cell corners, so diagonal corner motion does not inherit
the false positives of a merely expanded AABB. These queries return owning value receipts and
retain no PixelWorld or Physics pointer across calls.
