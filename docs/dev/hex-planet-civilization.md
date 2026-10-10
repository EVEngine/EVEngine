# Hex Planet civilisation: framework integration contract

This example is a compact turn-based civilisation race on the real spherical
hex topology. Economy is the stock authority, tactics owns expeditions and turns,
production owns building progress, and script ECS owns cities and their buildings.
It is an engine integration exercise with a playable victory condition, not a
complete Civilization ruleset.

## Play and acceptance

Right click a tile to move the expedition or select an owned city. White marks the
expedition, cyan cities belong to the player, amber marks selection, and green
marks reachable tiles. Drag left mouse to orbit; C focuses the expedition.
F founds a city for 30 gold, at least three graph steps from any existing city.
Enter ends a round. City buttons / 1–4 queue granary, mine, library or Beacon.
Writing costs 24 accumulated science; a Beacon also requires three cities.
The first civilisation to complete a Beacon wins. The opponent uses the same
movement, founding, payment and production commands as the player.

Set `EVE_HEX_CIV_TEST=1` before running the normal example to execute
`civilization_test.nut` at frame 100. It checks rejected movement/founding,
exactly-once charging, production completion, research, expansion, terminal
outcome and clean restart. It never calls MCP eval or assigns simulation results.
The regular smoke harness remains `MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh hex-planet`.

## State and lifecycle

- `Colony : eve.Entity` is the sole city family. `ColonyState` owns tile identity,
  faction, population and completed buildings. This cold, local authoritative
  state is session-only. `ColonyWork` retains a generation-checked WorkQueue proxy
  and a task ID; there is no script progress counter.
- `ColonyTurnSystem` queries Colony with both components. It reads terrain and
  technology, writes city state and economy, and advances production once per
  round with an injected work delta. It creates/destroys no entities in a View.
  City ordering is sorted by tile ID so shared food consumption is deterministic.
- The campaign owns the tactics battle proxy; the tactics session owns battle,
  side and unit ECS lifetimes. City/selection lookups resolve tile identities
  each time rather than retaining city pointers across commands.
- Restart snapshots the city View, releases each queue, destroys cities, releases
  the battle, drains this example's resource keys and rebuilds from the seed.
  Queue/battle-first release makes old proxies stale. ECS-first teardown leaves
  destruction to the respective module owner; module/world teardown is governed
  by their existing ownerEpoch/generation contracts.
- Game script hot reload is disabled explicitly: session-only script ECS state
  has no versioned campaign save yet. New terrain / R starts a new campaign.
  Tactics snapshot tests cover graph topology, occupancy and stale proxy release;
  they do not imply whole-campaign persistence.
- Simulation runs on the main thread. The campaign has no random draws; seed
  controls terrain, stable tile ordering controls AI ties, and frames never
  advance production. Visual floating-point camera projection is not simulation.

## API defects found and repaired

### HEX-CIV-01: arbitrary graph edges could not express spherical adjacency

`BoardState::addEdge` previously required a neighbour already implied by Square4,
Square8 or HexAxial offsets. Sphere cell IDs are neither axial nor rectangular
coordinates; twelve cells have five neighbours. Encoding IDs as x coordinates
would silently permit unrelated consecutive IDs and reject real sphere edges.

`BoardTopology::ExplicitGraph` and script `setTopology("graph")` now use only
explicit outgoing edges. Coordinates are opaque stable identities in this mode.
The existing path solver, occupancy checks and movement-point debit are reused.
Grid behaviour is unchanged. The sphere adapter lives in the example, so tactics
adds no dependency on hexmap. Setup must choose topology before adding edges.

Graph neighbour lookup uses the ordered edge index: O(log E + outgoing degree).
Reachability is computed after commands and cached as a derived UI projection.
Grid-facing, coordinate-range, straight-line sight and cover queries explicitly
reject graph mode, because a graph has no embedded directions or Euclidean ray.

Graph snapshots emit `tactics:battle` schema 7 using the existing cell/edge
records. Grid snapshots continue emitting schema 6. Restore accepts versions 1–7
through the existing migration defaults, refuses graph topology before version 7,
rejects unknown versions/fields, and validates a candidate before committing.
Graph-facing state is neutral zero. Existing grid saves retain their meaning.

Regression evidence: `test/tactics_graph.cpp` builds an actual HexSphereTopology,
checks pentagons, rejects invented adjacency/reverse edges, exercises the script
binding and snapshot restore, and checks stale release.

### HEX-CIV-02: interface catalogue could not register the required module face

The architecture policy required a `module-interface` entry, but the executable
catalogue rejected that rule name. The rule is now accepted with all six faces,
thread affinity, costs, trim evidence and hot-path declarations required. Missing
binding/cost fields have regression coverage. This is declaration validation;
it does not claim new automatic symbol-level verification of every capability.

### HEX-CIV-03: sphere vector bindings returned opaque C++ Value objects

The first real camera consumer failed at `sphereDirection(cell)[0]`: both
`sphereDirection` and `sphereCornerDirection` returned a boxed C++ Value rather
than the documented native Squirrel array. The binding now copies xyz into an
`ssq::Array`. The script contract remains `[x,y,z]`, including the existing north-pole
direction for invalid cells. `test/hexmap_sphere_script.cpp` checks actual script
indexing, length, unit magnitude and the invalid-cell case without requiring GPU
initialization. The normal example exercises the same binding in a real frame.

### HEX-CIV-04: malformed play requests terminated Debug runtime

A real MCP `eve_play` observe request missing its schema fields triggered the
unchecked-Result destruction assertion instead of returning a protocol diagnostic.
PlayHost constructed several validation Results before checking the first; an
early return abandoned the others. Request, contract and observation parsing now
check each Result before constructing the next. Regression cases omit successive
required fields, including incomplete observation specifications. The required
schema/version and unknown-field rejection remain unchanged.

### HEX-CIV-05: field observation required unrelated runtime handles to serialize

PlayHost observed requested fields only after capturing every snapshot root and
native provider. Consequently a campaign with a tactics handle could not expose
its plain `turn` field. Observation now resolves only policy-eligible roots and
raw table paths, then uses common's bounded `valueFromSquirrel` converter on the
requested values. It does not invoke script getters or native snapshot providers.
Missing fields, unsupported selected values and excluded roots remain errors;
whole-root observation still requires the whole requested root to be convertible.
Checkpoint semantics are unchanged. The real-VM regression verifies owning data,
stack restoration and failures despite unselected/unrelated class instances.

### HEX-CIV-06: progress rendering ignored explicit widget dimensions

The city panel requested a 12-pixel production bar. Layout measured 12 pixels,
but the retained renderer called ImGui with its default height and full available
width, so the visible bar consumed more room than the layout contract promised.
The renderer now forwards each positive size axis and preserves automatic sizing
for unspecified axes. `test/ui_progress_size.cpp` checks actual ImGui content
extents for two different explicit sizes, rather than only reading stored props.
No public API, ECS lifetime or serialization format changes are involved.

## UI revision (2026-10-08)

The fixed 1280x800 HUD uses a top resource/objective strip, contextual city or
expedition panel, lower-right turn action and bounded three-entry dispatch log.
World/debug information is hidden until requested. Gold/food/science increments
show gross per-round production; growth consumes the shared food stock, explained
in the selected city. Rules queries are shared by presentation and commands, so
costs, building prerequisites and production estimates cannot drift independently.

Hover routes come directly from tactics `previewMove`, cached until the hovered
cell, turn or unit movement changes. Camera and map input reject HUD regions and
native UI capture. A failed move preserves selection. City names derive from their
stable tile identity; no new city identity or ownership store was introduced.
Completion, research and rival-founding notifications are bounded presentation
history, rebuilt on reset; they are not a second gameplay event authority.

## Boundaries and limits

R-FACE-1/2: tactics' six faces are recorded in architecture_contracts.json;
existing Link and ECS contracts remain their single declarations. R-MECH-1/2:
setup is direct one-to-one composition, turn execution is synchronous and ordered;
no new per-frame module lookup or capability registry was introduced.
R-COST-1: topology is populated through the existing battle builder, not a hidden
per-frame conversion. No new global gameplay base, mutable resource mirror,
parallel production queue or reverse include was introduced.

This slice covers peaceful exploration, settlement, construction, research and
an AI victory race. Combat, diplomacy, trade, campaign saves, naval travel and
geometric spherical fog are outside this slice. Explored cells are gameplay/UI
knowledge only; the globe remains visible. Existing economy debit still has a
legacy boolean contract; this consumer checks it and performs one-resource costs,
without inventing a multi-resource transaction API or discarding failures.

## Verification (Windows, 2026-09-22)

- MSVC Debug strict-warning build: `eve` and `unit_test` passed.
- `make check`: passed, including 180 Python tests and architecture/layering checks.
- CTest filter `^(tactics\.|hexmap\.sphere|production\.|economy\.|devtools\.playHost\.)`:
  148/148 passed.
- Real Vulkan acceptance journey: victory on turn 22, then successful clean restart.
- Actual UI click events: queue library, end five rounds, observe completion;
  player selection remains unchanged through rival expansion. Formal `eve_play`
  observe succeeds; mapped build-mine action starts the next production task.
  A missing-schema request returns an error without terminating the runtime.
- Repository smoke harness: alive for 15 seconds, no error markers. A private
  executable filename isolated the harness's Windows image-name termination
  from other tasks' engine processes.
- Engine-readback screenshot inspected: `examples/hex-planet/hex-planet.png`.
- Server `eve_profile_smoke` compiled without hexmap/graphics; provider-present
  and provider-absent capability probes passed. This is L1 compile evidence,
  not an L2 tactics link or whole-campaign save/hot-reload claim.

No architecture rule was waived. Existing economy debit is consumed with its
checked legacy contract; graph snapshots are versioned, mutation remains outside
ECS iteration, and operation Results are observed before early returns.

### UI verification (2026-10-08)

- 270 related native cases passed, including the new progress rendered-size case
  and the existing UI control, focus, overlay, world-anchor and gameplay cases.
- `make check CI_BASE=0182af046d0e0d63d7478e5a444c0f87047136bf` passed,
  including 180 Python tests. This is the task's actual starting commit and
  merge-base with the now-advanced origin/dev; the remote branch has moved since
  the original integration. No gate baseline/allowlist was added or suppressed.
- Real UI event requests rejected the disabled Beacon button, queued the library,
  disabled competing projects, completed it after five rounds, showed its dispatch,
  changed to expedition context and toggled world details both ways.
- Real-frame acceptance projected a reachable tile back through sphere picking,
  verified route costs and no movement side effects, and rejected HUD input.
- All five visible HUD hosts reported zero layout overflow. Final engine readback
  was inspected, and the normal example survived the repository's 15-second smoke.
