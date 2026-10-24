# Saving Games Across Modules (Current Source)

[中文](../../usr/SAVE.md) · [User guide](README.md)

This page is maintained by hand; the Chinese documentation is authoritative. Check that your SDK contains the APIs described here before using a `dev` feature.

Player saves combine gameplay domains using **JSON text, strict validation and atomic publication**. There is no separate universal save module.

## Principles

1. Each mutable fact has one authoritative owner, such as `GameState`, `Tracker`, a bag or a battle. Serialize that state.
2. For a cross-domain restore, use `GameSaveSession` to prepare the whole candidate before publication. A failed participant preparation leaves all domains unchanged. Separate script calls only restore their own domains.
3. Item, quest and ability definitions belong to content, not the player's save. Load matching `contentVersion` definitions before restoring.
4. Write to disk with `fs.writeTextAtomic` and check success. `fs` is the startup filesystem instance. Validate before restoring and try a backup if the primary save is invalid.

## Choose the save path

- **RPG:** `eve.RPGSaveSession()` coordinates `GameState`, `Tracker`, a character or `RPGParty`, `Bag` and `EquipmentSet`. See [rpg-classic](../../../examples/rpg-classic/) and the [RPG guide (Chinese)](../../usr/modules/rpg.md).
- **Inventory only:** `eve.InventorySaveSession()` coordinates `Bag` and `EquipmentSet`. See [inventory (Chinese)](../../usr/modules/inventory.md).
- **Tactics:** battle snapshot and optional command log preserve the tactics `Battle`. See [tactics example](../../../examples/tactics/) and [guide (Chinese)](../../usr/modules/tactics.md).

`examples/combat-arena` is a deterministic simulation demonstration, not a cross-session save acceptance example. Connect HP and cooldowns to RPG or your own authoritative state before persisting them.

## RPG: the recommended path

```squirrel
local session = eve.RPGSaveSession();
if (session.setContentVersion("mygame.content.v1") == 0)
    throw "invalid content version";
if (session.bindParty(gameState, tracker, party, bag, equipment) == 0)
    throw "save participants are missing";

// Snapshot returns a string (empty on failure); validation/restore/write return 1/0.
local snap = session.snapshotJson();
if (snap == "" || session.validateSnapshotJson(snap) == 0)
    throw "cannot snapshot this checkpoint";
if (fs.writeTextAtomic("slot1.json", snap) == 0)
    throw "cannot commit slot1.json";
local json = fs.readText("slot1.json");
if (json == "" || session.validateSnapshotJson(json) == 0)
    throw "missing or invalid save";
if (session.restoreSnapshotJson(json) == 0)
    throw "restore rejected; participants unchanged";
```

The first fragment uses party schema v2. Do not pass a party as the single actor to syncEquipModifiers. All fragments are integration code: `gameState`, `tracker`, `party`, `bag`, `equipment`, `actor` and `json` come from your game. Handle each failure before proceeding; do not restore a candidate that validation rejected.

The Chinese RPG guide covers content versions, ID renames, appended quest migrations and single-character-to-party migration.

## Tactics: snapshot and optional replay log

```squirrel

local baseline = battle.revision();
local doc = battle.snapshotJson();
if (!doc.ok) throw "battle snapshot failed";
// Advance the battle with accepted commands before capturing the optional log.
local log = battle.commandLogJson(baseline);
if (!log.ok) throw "battle command log failed";
local restored = battle.restoreJson(doc.value);
if (!restored.ok) throw "battle restore failed";
local replayed = battle.replayJson(log.value);
if (!replayed.ok) throw "battle replay failed";
```

The log must be anchored to the snapshot's baseline. HP or cooldowns owned by ECS/RPG are not automatically included in a tactics snapshot.

## C++: restore RPG and tactics together

Register an already-bound `RPGSaveSession` through `RPGGameSaveParticipant`, and a live `Battle` through `TacticsGameSaveParticipant`, in one `eve::GameSaveSession(contentVersion)`. The adapters and their borrowed domain owners must outlive the coordinator. Perform save and load at a paused boundary on the simulation thread. The [integration test](../../../test/integration/gameplay-composition/gameplay_composition_graphics_gameplay_action_tests.cpp) shows the complete C++ wiring.

`snapshotJson(hashProvider)` produces one sealed JSON document. Write that document to one slot with a checked atomic file write. On load, restore matching content definitions first, then call `restoreJson(json, hashProvider)` on the primary or backup. The coordinator checks integrity, schema, content version and the exact participant set; every participant prepares a candidate before any state is published. An error leaves all live domains unchanged. Register explicit `addContentMigration` functions for older content versions; migrations operate on unpublished JSON. Use the same injected digest algorithm for save and load. The digest detects accidental corruption and does not authenticate the save.

The Squirrel bindings still expose independent RPG and tactics restore calls. Games requiring the cross-domain transaction must wire this C++ coordinator rather than treating two script calls as atomic.

## Save slots and backups

```squirrel

if (fs.writeTextAtomic("save-slot1.json", jsonText) == 0)
    throw "save write failed";
local text = fs.readText("save-slot1.json");
if (text == "") throw "save absent or unreadable";
```

Check the return contract of file operations. Paths must stay inside the configured save directory. WebGPU currently cannot provide the same atomic replacement guarantee and reports failure; see [files and hot reload](modules/filesystem.md).

Initialize the writable identity with checked setIdentity/setupWriteDirectory calls; config.identity does not do this.
Validate the old primary before copying it to the backup. Check that backup writing succeeded before replacing the primary.
If both candidates are invalid, keep the existing game state. A single-file atomic write does not provide a cross-domain transaction.
The [complete tutorial (Chinese)](../../usr/GAME_DEVELOPMENT.md) and its [actual game source](../../../examples/game-tutorial/main.nut) implement this flow.

`rpg-classic` rotates three slot files and `.backup.json` files, and lists only slots passing `validateSnapshotJson` on the title screen.

## Avoid these mistakes

- Using engine/DevTools debug snapshots as the player-save format.
- Mutating authoritative containers to test a restore before validation succeeds.
- Saving rebuildable equipment modifiers, render caches or transient event queues.
- Assuming a tactics snapshot also owns RPG state.

See [RPG](../../usr/modules/rpg.md), [inventory](../../usr/modules/inventory.md), [tactics](../../usr/modules/tactics.md) and [the 0.6 closeout checklist](../../dev/2026-10-08-0.6-closeout-checklist.md) for domain-specific details; these pages are currently in Chinese.
