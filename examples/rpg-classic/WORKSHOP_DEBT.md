# RPG Classic — workshop quality debt (Slice E)

Scoped notes for the agent task-profile plan. Full atelier FINDINGS are not
checked in; keep remaining map-editor debt here so it does not block A–D.

| ID | Status in this example | Notes |
| --- | --- | --- |
| EM12 | Fixed | `persist battleLog` (UI id remains `"log"`) — matches `examples/rpg` |
| EM4 | Fixed | `persist gameState` + `game.agent.json` observe path `gameState` |
| EA5 | Pattern only | `workshopInputMode()` / `gameState.mode` separate battle digits from explore keys; no F1–F4 editor fight here |
| EA2 | Deferred | No TileBuffer/brush authoring in this slice; `eve_workshop` `map.brush` returns `Unsupported` until a live editor session exists |
| EA6 | Deferred | No full-layer dirty sync sample; use brush `getChange*` when a map-edit workshop lands |
| EQ4 | Partial | `game.agent.json` smoke requires `rpg-alive` observe (not process-alive alone) |

Do not create a top-level `rpg_atelier` module package; workshop surface stays
`eve_workshop` + this example.
