# Saving Games Across Modules (0.6)

[中文](../../usr/SAVE.md) · [User guide](README.md)

This page is maintained by hand; the Chinese documentation is authoritative. Check that your SDK contains the APIs described here before using a `dev` feature.

Player saves combine gameplay domains using **JSON text, strict validation and atomic publication**. There is no separate universal save module.

## Principles

1. Each mutable fact has one authoritative owner, such as `GameState`, `Tracker`, a bag or a battle. Serialize that state.
2. Validate the entire candidate before publishing it. If a participant fails, all participants keep their original state.
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
session.setContentVersion("mygame.content.v1");
// New games use party schema v2.
session.bindParty(gameState, tracker, party, bag, equipment);
// Legacy single-character path:
// session.bind(gameState, tracker, actor, bag, equipment);

local snap = session.snapshotJson(); // Strict envelope and digest.
// The script binding returns a string (empty on failure), not a Result table.
// Atomic writing returns 1/0 in scripts.
if (snap == "" || fs.writeTextAtomic("slot1.json", snap) == 0) {
    print("save failed\n");
}

// Load matching item/quest/ability definitions before restoring.
// Validation and restore both return 1/0 in scripts.
if (session.validateSnapshotJson(json) != 0 && session.restoreSnapshotJson(json) != 0) {
    rpg.syncEquipModifiers(actor, equipment); // Rebuild the equipment projection.
} else {
    print("restore failed; try a backup\n");
}
```

These are integration fragments: `gameState`, `tracker`, `party`, `bag`, `equipment`, `actor` and `json` come from your game. Handle each failure before proceeding; do not restore a candidate that validation rejected.

The Chinese RPG guide covers content versions, ID renames, appended quest migrations and single-character-to-party migration.

## Tactics: snapshot and optional replay log

```squirrel
local baseline = battle.revision();
local doc = battle.snapshotJson();
// After advancing the game, capture commands accepted after baseline.
local log = battle.commandLogJson(baseline);

// Check both Results before accessing their values.
if (doc.ok && log.ok) {
    local restored = battle.restoreJson(doc.value);
    if (restored.ok) {
        local replayed = battle.replayJson(log.value);
        if (!replayed.ok) print(replayed.status.summary + "\n");
    } else {
        print(restored.status.summary + "\n");
    }
} else {
    print("snapshot or command log failed\n");
}
```

The log must be anchored to the snapshot's baseline. HP or cooldowns owned by ECS/RPG are not automatically included in a tactics snapshot. Save them separately and restore in a defined order. A single transaction combining board and RPG state is a 0.7 roadmap goal; 0.6 callers coordinate it explicitly.

## Save slots and backups

```squirrel
if (fs.writeTextAtomic("save-slot1.json", jsonText) == 0) print("save failed\n");
local text = fs.readText("save-slot1.json");
```

Check the return contract of file operations. Paths must stay inside the configured save directory. WebGPU currently cannot provide the same atomic replacement guarantee and reports failure; see [files and hot reload](modules/filesystem.md).

`rpg-classic` rotates three slot files and `.backup.json` files, and lists only slots passing `validateSnapshotJson` on the title screen.

## Avoid these mistakes

- Using engine/DevTools debug snapshots as the player-save format.
- Mutating authoritative containers to test a restore before validation succeeds.
- Saving rebuildable equipment modifiers, render caches or transient event queues.
- Assuming a tactics snapshot also owns RPG state.

See [RPG](../../usr/modules/rpg.md), [inventory](../../usr/modules/inventory.md), [tactics](../../usr/modules/tactics.md) and [the 0.6 closeout checklist](../../dev/2026-10-08-0.6-closeout-checklist.md) for domain-specific details; these pages are currently in Chinese.
