---
name: eve-action-guide
description: EVEngine 2D action guide-pack pipeline. Use when exporting whitebox silhouette/depth/bone AOVs, ingesting AI-generated sprite frames, drafting Ability timelines, or verifying guide IoU.
---

# 2D action guide → AI frames → ingest (EVEngine)

Design: `docs/dev/2026-11-03-2d-action-ai-guide-pipeline.md`.
Schema: `eve.action.guide-pack` v1. Module notes: `docs/usr/modules/action.md`.

## Role split

| Who | Owns |
| --- | --- |
| Engine | pose / skeleton authority, guide AOV production, SpriteSheet/clip ingest, Ability draft bind, playable verify host |
| External agent | prompts, model choice, seeds, candidate pre-filter |

Engine does **not** own “how pixels look”; it owns geometry, camera contract,
conditioning packs, and post-ingest playability.

## Pipeline

```text
GuidePack doc → sample pose (no live gameplay) → AOV render
  → guide-pack/ (manifest + shots/*/silhouette|depth|bone.png)
  → external AI → candidate frames
  → eve_action_guide_ingest → sequence + ability.draft.json
  → eve_action_guide_verify (IoU / joints) → eve_play evidence
```

MCP family (two-step expensive ops — request → token → status → export):

- `eve_action_guide_pack_*` — document CRUD
- `eve_action_guide_landmarks` — bone projection
- `eve_action_guide_capture` / `_status` / `_export` — offscreen AOVs
- `eve_action_guide_ingest` — validate candidates; write draft Ability JSON
  (does **not** register the live Ability)
- `eve_action_guide_verify` — silhouette IoU (+ optional joint error) →
  `verify.evidence.json`

Missing graphics provider → `ProviderAbsent` (explicit), not a silent fake pack.

## Contracts

1. One fact, one owner — pack revision + pose revision in metadata; AI output
   carries `sourceGuideRevision`; ingest rejects stale packs.
2. Prefer domain ids + generation (same as entity views) — no new GameObject root.
3. Tag clips with action semantics (`idle` / `run` / `attack.light` / …), not
   filenames-as-protocol.
4. Close with `.cursor/skills/eve-agent-loop/SKILL.md` evidence, not “looks fine”.

## Do not

- Use desktop/Xvfb framebuffer as guide or acceptance images.
- Treat ingest draft as a granted Ability until explicitly registered.
- Embed cloud diffusion keys or model weights inside the engine.

## Gold

- Fixture: `docs/dev/fixtures/eve.action.guide-pack.v1.example.json`
- External orchestration references: `tools/asset-pipeline-agent/`
- Animation / Action manuals: `docs/usr/modules/animation.md`, `action.md`
