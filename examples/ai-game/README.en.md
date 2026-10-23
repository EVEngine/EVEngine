# AI Game — Drive a Live Game through MCP

[中文](README.md) · [AI/MCP setup](../../docs/en/usr/AI-MCP.md)

A small real 2D combat game demonstrating the **Play Host**. `game.agent.json` declares the observation contract. Agents use `eve_play` to pause, advance frames, observe, capture and restore checkpoints. Use that contract for gameplay observations rather than treating `eve_eval` as the official state-reading path.

## Start

From the repository root with a matching engine:

```sh
eve run --debug --mcp-port=7529 examples/ai-game
# Source build:
make run/<platform>-debug GAME=examples/ai-game RUN_ARGS="--debug --mcp-port=7529"
```

This repository example is not bundled with the SDK. [Obtaining examples](../README.en.md) explains source/version requirements.

## Agent workflow

Calls to `eve_play` contain a `request` object with `schemaId: "evengine.play-request"` and `schemaVersion: 1`:

- `op=status`: discover the game.agent.json contract, clock state and gameplay domains.
- `op=capture`: capture an engine-readback PNG.
- `op=clock`, `mode=pause`: pause the host clock.
- `op=observe`, `observation=combat-alive`: read declared gameState fields.
- `op=step`, `clock=frame`, `count=N`: run N frames, then pause.
- `op=checkpoint`, `mode=capture` or `restore`: record/restore the declared script roots.

For exact requests, read `agent_demo.py`. Lower-level pause/eval tools exist, but this demo's gameplay acceptance path does not depend on them.

Use `eve_restart` for iterative soft restarts, optionally passing arguments such as seed/enemyHp. It retains resource caches. With `reloadScripts=false`, the script's `eve_restart(args)` implements a quick restart.

The example also uses `eve_agent_session_*` to track a versioned development session: declare acceptance criteria, collect evidence, reach Verify and only then complete the session.

## Reproduce without an LLM

Leave the game running and use a second terminal:

```sh
python examples/ai-game/agent_demo.py 7529
```

The standard-library TCP client captures, pauses, observes, records a checkpoint, advances the clock and verifies tick progression, then restores and verifies rollback. It prints `PASS` when its assertions succeed.

Capture happens before pause because a paused loop does not present a new frame. The driver treats unavailable screen readback as an optional warning; observation, stepping and restore remain strict assertions. Thus a driver PASS alone is not proof that a screenshot was produced.

## Script structure

`main.nut` separates authoritative plain data from command APIs:

- Observations include gameState tick/time and player/enemy health.
- `game.setEnemyHp`, `game.setPlayerHp` and `game.reset` are developer cheats, not the Play Host command contract.
- `persist gameState` retains state through hot reload; `eve.dev.markStateRoot("gameState")` explicitly includes the same root in checkpoints.

Agents can connect through the [stdio-to-TCP bridge](../../tools/eve-mcp/README.md) or a TCP client. Normal hot reload remains available.
