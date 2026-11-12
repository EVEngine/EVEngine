# EVEngine agent skills

Short, triggerable playbooks for coding agents. Each skill stays under ~120 lines:
entry rules, do-nots, ownership map, and gold examples. Full manuals stay in
`docs/usr/` / `docs/dev/`; callable signatures stay in Binding Contracts /
`eve_api_search`.

| Skill | When to load |
| --- | --- |
| [`evescript`](evescript/SKILL.md) | Writing `.nut`, `eve.*` APIs, language / concept map |
| [`eve-agent-loop`](eve-agent-loop/SKILL.md) | Discover → Modify → Run → Observe → Verify sessions |
| [`eve-scene-search`](eve-scene-search/SKILL.md) | Semantic object discovery, spatial frames, MCP query / paging / recovery |
| [`eve-game-loop`](eve-game-loop/SKILL.md) | New playable game: authority, persist, save, HUD |
| [`eve-arpg`](eve-arpg/SKILL.md) | Action RPG: action → combat → settlement → RPG/inventory |
| [`eve-rpg`](eve-rpg/SKILL.md) | Attributes, buffs, skills, vitals, quests, turn battle |
| [`eve-rts`](eve-rts/SKILL.md) | Fixed-step RTS: factions, commands, fog, production |
| [`eve-action-guide`](eve-action-guide/SKILL.md) | 2D whitebox guide AOV → AI frames → Ability draft |

SDK zip installs these under `.cursor/skills/<name>/SKILL.md`. The language skill
is also copied to `share/eve/ai/SKILL.md` for older entry points.
