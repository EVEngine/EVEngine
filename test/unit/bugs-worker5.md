# Worker 5: confirmed defects

## Melee attack emits two `fire` events for one active-stage transition

- **Reproduction:** Create a `Weapon` with `{"id":"sword","kind":"melee","logic":"melee","stages":{"windup":0.25,"active":0.10,"recover":0.35}}`. Call `attack`, then `update(0.3f)` and `update(0.2f)`. The expected event sequence is `windup_start`, `fire`, `attack_end`.
- **Observed:** `weapon.spec.meleeStagesAndResource` received `fire` at event index 2, where `attack_end` was expected. Reproduced by `ctest` in `build/rewrite-ctest-domains-b.log` (test 4907).
- **Cause:** `WeaponSystem::updateStage` calls `spawnPellets` and then `pushFireEvent` when windup ends (`src/modules/weapon/WeaponSystem.cpp:349-351`). `spawnPellets` invokes `MeleeLogic::fire` (`:136-145`), which itself calls `pushFireEvent` (`:67-75`). Both paths publish the same fire transition.
- **Impact:** Consumers of the event stream can apply a melee hit, visual cue, or sound twice for one attack. The behavior test retains the one-fire contract and therefore remains failing until the implementation is corrected.
