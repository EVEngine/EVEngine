# Three Sentinels — ARPG closed-loop slice

Run `make run/linux-debug GAME=examples/arpg-loop` (or the host platform).
The bundled DejaVu font's license is included under assets/fonts/NOTICE.txt.

Camp is the safe hub on the left; the arena is on the right. T enters the arena,
G returns after a kill. WASD moves, J attacks, K winds up a heavy hit, Space dodges.
Orange squares telegraph an enemy strike; move out or dodge before it lands. After
each kill, E near the gold altar claims a blade and Q equips it (+8 attack). N or T
starts the next sentinel. Defeat three to complete the main quest.

At camp: F talks to the scout (manual side quest → claim gold after any kill), B buys
a camp potion (−15 gold; fails without gold and must not touch quest state), U drinks
it. F5 saves only at camp; F9 loads back to camp. R restarts.

The two-to-three-slot bag can fill: unclaimed caches remain in the world and are saved.
Equipping frees a slot. A checkpoint captures XP, gold, quests, bag, gear and consumed
kill/cache identities; loading never repeats a kill reward or collected cache.

The C++ ActionEncounter routes admitted hits through action's combat:damage registry
into RPG Vitals, retains failed reward deliveries, and validates actor generations.
The script owns zones, fixed-step movement, range, telegraphs, attack timing and dodge
admission; it never maintains a second health value. Enemy deaths commit progression
and unlock static caches; pickup is a separate WorldInteraction transaction. Saves are
camp checkpoints, not mid-fight replay snapshots. This is a local single-player slice,
not a network-authoritative implementation.

`game.agent.json` exposes the same input commands through eve_play for repeatable
acceptance. `agent_check.py` checks camp/arena travel, combat, growth, side-quest claim,
shop failure/success, pickup, equipment and checkpoint restoration without eve_eval,
and captures engine readback evidence.

Acceptance path:

```sh
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh arpg-loop
bash scripts/verify_arpg_loop.sh   # or: make check/arpg-loop
```

`verify_arpg_loop.sh` starts `eve run --debug --mcp-port=…` and runs `agent_check.py`.
Final accepted restore state: level ≥2, 23 gold (30 from kills −15 potion +8 scout),
26 attack, completed main + side quest, two bag blades plus equipped weapon, camp zone.
