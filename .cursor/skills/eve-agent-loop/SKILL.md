---
name: eve-agent-loop
description: EVEngine agent game-development loop. Use when verifying gameplay with MCP, eve_play, agent sessions, evidence, screenshots, or play-verify-recover iteration.
---

# Agent development loop (EVEngine)

Do not claim a game change is done from compile output alone. Close the loop on a
real frame path with evidence. Full protocol: `docs/dev/Agent游戏开发范式.md`.

## Loop

```text
Discover -> Modify -> Run -> Observe -> Verify -> Complete
                 \________________ Recover ______________/
```

1. **Discover** — `eve_game_world` / `eve://game-world` / `eve_play` `status.gameWorld`;
   read `game.agent.json` (Play entry, observe surface, legal actions, screenshot
   backend). Enumerate MCP tools. Without a contract, `observe` may be `Unsupported`.
2. **Modify** — edit `config.nut` / `main.nut` / assets / Editor targets. Side effects
   only via save, hot-reload, or Editor transaction.
3. **Run** — real frame loop or deterministic smoke/update path.
4. **Observe** — runtime query, RX, UI semantic tree, or engine screenshot
   (`eve_screenshot` / play capture). Never desktop/Xvfb framebuffer as evidence.
5. **Verify** — attach evidence to the criterion ids declared at session start.
6. **Recover** — restore snapshot, undo transaction, fix files, rediscover identity;
   then Modify/Run again.
7. **Complete** — only when every required criterion has passing evidence and the
   session phase is Verify.

## Session protocol

Start with `eve_agent_session_start` (`objective`, stable `criteria` ids, optional
`taskKind` / `profile`; default `verify` / `verify.play` — narrow play surface).

- Advance with `eve_agent_session_advance` (phase graph; skipping Run/Observe →
  Verify returns `invalid-transition`).
- Append evidence with `eve_agent_session_evidence` (immutable receipts; later fail
  revokes the same criterion’s pass).
- `manual` notes do not alone pass a required criterion.
- Complete only via `eve_agent_session_complete`; otherwise `invalid-phase` /
  `acceptance-incomplete`. Abort with `eve_agent_session_abort` when stuck.
- Escalate tool surface with `eve_agent_session_escalate`; out-of-profile calls
  return `filtered`.

## Authority (do not fork state)

- Session owns objective / phase / criteria / evidence metadata only.
- Game state stays in Squirrel / ECS / SceneHost / Renderable3D.
- Editor documents stay in Editor transactions.
- Snapshots, tests, screenshots, artifacts keep their own owners; ledger stores
  summaries and paths.

## Do not

- Teach `eve_eval` as the official way to change HP or skip puzzles.
- Treat compile-only as “playable”.
- Use Xvfb/desktop capture as visual acceptance.

## Gold paths

- `examples/ai-game/agent_demo.py` — attach → mutate → observe → snapshot recover → evidence
- `examples/ai-editor/editor_demo.py` — Editor transaction → RX converge → undo → screenshot
- Play Host: `docs/dev/2026-09-05-play-verify-evolve.md`
