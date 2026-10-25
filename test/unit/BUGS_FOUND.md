# Defects found while rewriting unit tests

Record input, expected behavior, observed behavior, source location, and reproduction status.

## Reproduced contract mismatches

### Battle settlement changes attribute HP but leaves current vitals unchanged

- Input: create a hero with 30 attack and an enemy with 20 current/max HP, queue one ordinary attack, then execute its first action.
- Expected: lethal damage sets the enemy's current HP to 0, marks the enemy dead, and finishes the battle in victory.
- Observed: `VitalsSystem::getCurrent(enemy, "hp")` remains 20; the focused Windows CTest fails at `20 == 0`. The event stream nevertheless ends with `kill`. `RPGSettlementAdapter::prepareApply` writes `target_.attributes()->values` (`src/modules/rpg/SettlementAdapter.cpp:428-485`), while `VitalsSystem::getCurrent` and `isDead` read `actor->vitals()->current` (`src/modules/rpg/VitalsSystem.cpp:27-40`, `src/modules/rpg/Battle.cpp:125-127`). `publishSettledChange` only emits events, so the two HP stores diverge.
- Resolution: the settlement adapter now reads and commits current health in Vitals, keeps the maximum in AttributeSet, and rolls both stores back together. The battle and mitigation regressions plus the settlement replay integration case pass locally on Windows.

### Melee stage transition emits duplicate `fire` events

- Input: register a melee `sword` with `windup=0.25`, `active=0.10`, `recover=0.35`; call `attack`, `update(0.3f)`, then `update(0.2f)`.
- Expected: one attack produces `windup_start`, one `fire`, then `attack_end` (3 events).
- Observed: 4 events; `weapon.spec.meleeStagesAndResource` fails with `getEventCount() == 3` expanding to `4 == 3`. `WeaponSystem::updateStage` calls `spawnPellets`, whose `MeleeLogic::fire` publishes `fire`, then publishes another `fire` directly (`src/modules/weapon/WeaponSystem.cpp:67-75,136-145,349-351`). Event consumers can apply a melee hit or cue twice.
- Resolution: the staged attack publishes the single fire event; `MeleeLogic::fire` no longer publishes a second one. The one-fire regression expectation remains in the test. Details are also in `bugs-worker5.md`.

### Spatial index header contracts disagree with implemented mutations

- Input: insert the same ID twice into `QuadTree`, insert a valid box beyond its root, and change `SpatialHash2D` cell size after two entries are present.
- Expected by headers: `QuadTree::insert` returns false for an existing ID or out-of-bounds box; `SpatialHash2D::setCellSize` clears existing entries.
- Observed: `QuadTree::insert` returns true and replaces a duplicate; it also retains a valid box beyond the root in its ID map. `SpatialHash2D::setCellSize` rebuilds occupied cells and retains both entries. The declarations in `src/modules/spatial/QuadTree.h` and `SpatialHash2D.h` differ from `QuadTree.cpp` and `SpatialHash2D.cpp`.
- Status: reproduced by `Spatial 2D index contract` in the Windows core CTest run; the revised test now states and checks the implemented behavior. Header documentation still needs a contract decision and correction.

## Code-path findings awaiting executable reproduction

### `SensingWorld::remove` invalidates the last query for an unknown ID

- Input: upsert `subject`, call `circle(...)`, then call `remove("missing")` and read `resultAt(0)`.
- Expected: `remove` returns `NotFound` without invalidating the prior query; the `resultAt` lifetime documentation does not list removal as an invalidator.
- Observed code path: `src/modules/sensing/Sensing.cpp` clears `results_` before looking up the ID, so `resultAt(0)` becomes empty even though no subject was removed.
- Status: source inspection only; compiled reproduction remains pending.

### `SpaceQuery::regionsConnected` accepts a non-region ID

- Input: after `Document::bootstrap("site", "building", "level0")`, call `SpaceQuery::regionsConnected("site", "site")`.
- Expected: reject the site ID because the API takes region IDs.
- Observed code path: `src/modules/archspace/ArchSpaceQuery.cpp` checks existence but not `NodeKind::Zone`, then returns `true` for equal IDs.
- Status: source inspection only; a compiled regression test has not run.

### `EffectContainer::apply` Replace can mutate state before identity failure

- Input: an existing effect, a valid `StackMode::Replace` definition, and an injected UUID entropy source that fails.
- Expected: failed application leaves effects and emitted events unchanged.
- Observed code path: `src/modules/effects/EffectContainer.cpp` erases matching effects and emits removal events before calling `effectIdGenerator_->generate()`, whose failure returns an error.
- Status: source inspection only; a compiled failure-injection test has not run.

### `VirtualTextureStreaming::takePendingUploads` permits a second take

- Input: stage an upload batch, take it once, then call `takePendingUploads()` again before commit or abandon.
- Expected: the header documents failure while the first batch awaits resolution.
- Observed code path: `src/modules/graphics/VirtualTextureStreaming.cpp` stores an engaged empty sentinel and the second take succeeds with that sentinel.
- Status: source inspection only; the known-failing regression expectation is documented in `bugs-worker4.md` and has not been run.

### `DataView::clone` retains a borrowed pointer despite deep-copy documentation

- Input: create a `DataView` over `ByteData`, clone the view, then destroy the original `ByteData` before using the clone.
- Expected: `DataView.h` documents `clone()` as a deep copy, so the cloned data should remain readable independently.
- Observed code path: `src/modules/data/ByteData.cpp` implements the `DataView` copy constructor as `data(d.data)`; `clone()` calls that constructor and still points at the original `ByteData`.
- Status: source inspection only; the dangling-pointer scenario is intentionally not executed in the suite.
