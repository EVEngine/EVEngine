# Three Sentinels — ARPG closed-loop slice

Run `make run/macosx-debug GAME=examples/arpg-loop` (or the host platform).
The bundled DejaVu font's license is included under assets/fonts/NOTICE.txt.

WASD moves, J attacks, K winds up a heavy hit, Space dodges. Orange squares telegraph
an enemy strike; move out or dodge before it lands. After each kill, E near the gold
altar claims a blade and Q equips it (+8 attack). N starts the next sentinel.
Defeat three to complete the quest. R restarts; F5 saves between fights and F9 loads.
The two-slot bag can fill: unclaimed caches remain in the world and are saved.
Equipping frees a slot. A checkpoint captures XP, gold, quests, bag, gear and consumed
kill/cache identities; loading never repeats a kill reward or collected cache.

The C++ ActionEncounter routes admitted hits through action's combat:damage registry
into RPG Vitals, retains failed reward deliveries, and validates actor generations.
The script owns fixed-step movement, range, telegraphs, attack timing and dodge admission;
it never maintains a second health value. Enemy deaths commit progression and unlock
static caches; pickup is a separate WorldInteraction transaction. No random loot is
used in this slice. Saves are checkpoints, not mid-fight replay snapshots. Loading
returns to camp; N rebuilds the next encounter with fresh handles and stable life IDs.
This is a local single-player slice, not a network-authoritative implementation.

`game.agent.json` exposes the same input commands through eve_play for repeatable
acceptance. `agent_check.py` checks combat, growth, pickup, equipment and checkpoint
restoration without eve_eval, and captures engine readback evidence.


Acceptance on macOS Debug: 14 native composition cases passed, source-quality gate
passed, three-second example smoke passed. Run the live driver with
`python3 examples/arpg-loop/agent_check.py 7538 /tmp/arpg-loop-evidence` after starting
`eve run --debug --mcp-port=7538` from this directory. It records observations and an
engine-readback image. Final accepted state: level 3, 30 gold, 26 attack, completed
quest, two bag items plus equipped weapon, and no remaining unclaimed caches.
