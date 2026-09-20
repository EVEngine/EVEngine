# T2b — Action economy, reaction/interrupt systems, determinism, replay, networking

> ## ⚠️ RECONSTRUCTION NOTICE — READ FIRST
>
> **This file is a PARTIAL reconstruction, not a faithful re-emit.** The original was 1,162 lines /
> ~30 KB and was destroyed. During the original research I read only **lines 820–1164** of it in
> full (the runtime-capability tables, the source-quality tiering, and the "highest-value next
> reads" list), plus a **heading index** of the rest. **Lines 1–819 — the detailed Topic A / B / C
> evidence sections — were never in my context and I cannot reproduce them.**
>
> What follows is therefore:
> - **§2, §3, §4 below — reproduced from what I actually read** (near-verbatim; capability tables,
>   source tiering, next-reads). These are trustworthy as a re-emit.
> - **§1 below — the heading index only**, with the subagent's own closing summary of what each
>   section contained. The body text is **NOT reconstructed**.
> - **§5 — an honest statement of what is permanently lost.**
>
> The consolidated document (`tbs-gap-analysis-research.md`) absorbed most of the *substantive*
> Target 2 findings from this file, so the loss is mitigated but not eliminated: the per-section
> URLs and the finer-grained citations that only existed here are gone.
>
> Evidence tags as in the original: **[TITLE-VERBATIM]**, **[TITLE]**, **[SNIP]**, **[INFER]**,
> **[UNVERIFIED]**.

**Method note (unchanged from the original).** `web_fetch` was non-functional in this session
(every URL → *"URL hostname resolves to a non-public IP address"*) and direct HTTP from the shell
also failed. The **only** research channel was `web_search`. **No page was ever opened.** The
original run was 27 `web_search` batches, ~105 narrow queries.

---

## 1. Topic A / B / C — heading index and scope (BODY NOT RECONSTRUCTED)

The original file's structure, recovered from a heading grep:

| Line | Heading |
|---|---|
| 7 | `## 0. Methodology, evidence grading, and a hard constraint` |
| 44 | `## A1. The dominant action-economy shapes` |
| 108 | `## A2. Overwatch / reaction fire / opportunity attacks / zone control` |
| 147 | `## A3. Interrupt windows, trigger stacks, and nested triggers — the canonical formal model` |
| 269 | `## A4. Determinism of reactions: ordering, tie-breaks, simultaneity, non-reentrancy` |
| 365 | `## A5. Topic A coverage gaps (explicitly unverified)` |
| 387 | `## B1. Deterministic lockstep and deterministic simulation` |
| 439 | `## B2. Fixed-point vs floating-point math` |
| 474 | `## B3. Seeded RNG streams and splitting streams per subsystem` |
| 514 | `## B4. Command pattern, event sourcing, command log vs state snapshot` |
| 568 | `## B5. Replay and undo in turn-based games` |
| 607 | `## B6. Rollback netcode and why TBS usually does not need it` |
| 657 | `## B7. Topic B coverage gaps (explicitly unverified)` |
| 678 | `## C1. Authority models` |
| 707 | `## C2. Hotseat / pass-and-play, async / play-by-mail / correspondence` |
| 768 | `## C3. Desync avoidance, detection, and resync` |
| 800 | `## C4. Backends and platform services` |
| 815 | `## C5. Topic C coverage gaps (explicitly unverified)` |

**What the subagent reported each section established** (from its closing summary — this is a
*summary of a summary*, so treat it as an index to content that no longer exists, not as the content
itself):

- **A1–A4** — action-economy shapes; overwatch/reaction-fire/opportunity attacks/zone control;
  interrupt windows and trigger stacks, for which **MTG Comprehensive Rules are the canonical formal
  model**; and reaction determinism (ordering, tie-breaks, simultaneity, non-reentrancy).
- **A3's headline claim:** the indexer returned **verbatim CR sentences as result titles** —
  CR 116.x, 117.3b, 403.4, 405.6d, 500.4, 602.5b, 603.2b/6c/7d, 608.2b/2d, 614.14, 616.1 — and the
  core lesson is that **MTG separates three resolution mechanisms** (replacement/prevention
  effects, uninterruptible state-based actions, and a LIFO trigger stack) rather than one event bus.
- **B1–B6** — lockstep + float determinism (**Gaffer On Games**), fixed-point vs float, seeded RNG
  stream splitting, command pattern / event sourcing / command-log vs snapshot, replay and undo, and
  rollback.
- **B5's headline claim:** the best evidence for *"undo ⇒ snapshot, not inverse commands"* is the
  **shipped Into the Breach bug "Undo move button can make smoke disappear."**
- **B4/B5 reference set:** **Gaffer On Games** (lockstep + floating-point determinism) and **three
  GDC determinism talks** (For Honor ×2, Warhammer AoS: Realms of Ruin).
- **B6's headline claim:** **no source argues that TBS does not need rollback** — that argument is
  flagged as **inference only**.
- **C1–C4** — authority models; hotseat / async / play-by-mail; desync avoidance, detection and
  resync; backends and platform services.
- **C3's headline claim (biggest hole in Topic C):** **zero practitioner writeups on desync
  detection / state hashing for TBS** — only patents and player anecdotes.
- **C4's confirmed backends (title-level only):** Nakama authoritative matches, Photon
  (`PunTurnManager` + Quantum fixed-point ECS), brainCloud Async Match, Beamable async
  notifications, Colyseus example, Steamworks lobbies/notifications; **PlayFab appears to lack any
  turn-based API**.

> **These bullets are a lossy pointer.** The original sections contained the per-claim URLs. If this
> material matters, it should be re-researched — a working fetch path plus ~25 searches would
> reconstruct it.

---

## 2. Runtime capabilities a complete TBS engine must provide

*(Reproduced from lines 828–1073 — this part I read in full.)*

Each item names the capability, then the source that motivates it. Where the motivation is my
inference it is marked `[motivation: INFER]`.

### Simulation determinism layer

1. **Injected simulation clock and fixed-step turn advance.** No wall-clock time inside
   simulation; the turn is the unit of advance.
   *Motivation:* deterministic lockstep requires bit-identical simulation
   ([Gaffer On Games — Deterministic Lockstep](https://www.gafferongames.com/post/deterministic_lockstep/));
   the Age of Empires technique schedules commands by lockstep turn
   ([Gamasutra AoE article (WPI mirror)](http://web.cs.wpi.edu/~claypool/courses/4513-B03/papers/games/aoe.pdf#2#1)).
2. **Deterministic numeric substrate (fixed-point or integer) for simulation state, with a written
   float-parity policy.** Floats in presentation are fine; floats in state are not.
   *Motivation:* float non-associativity
   ([arXiv 2104.06262](https://export.arxiv.org/pdf/2104.06262#5#2)); platform float divergence
   ([Microsoft — Sinus-Berechnung auf der xBox](https://learn.microsoft.com/bg-bg/archive/blogs/twendel/sinus-berechnung-auf-der-xbox-edt-120909));
   purpose-built FP math for deterministic games
   ([Photon Quantum — Fixed Point Math](https://doc.photonengine.com/zh-cn/quantum/v3/manual/quantum-ecs/fixed-point));
   cross-platform determinism as a shipped-game talk
   ([GDC — Cross-Platform Determinism in Warhammer Age of Sigmar: Realms of Ruin](https://gdcvault.com/play/1034229/Cross-Platform-Determinism-in-Warhammer));
   policy-document pattern ([crimson float-parity-policy.md](https://github.com/banteg/crimson/blob/master/docs/rewrite/float-parity-policy.md#1)).
3. **Named, independently-seeded RNG streams, one per subsystem (combat, AI, mapgen, FX), plus a
   match seed stored in the save/replay.**
   *Motivation:* multiple streams/sequences as a first-class RNG feature
   ([PCG — Useful Features: multiple streams, sequences](https://pcg-random.org/useful-features.html#multiple-streams-sequences));
   stream isolation patterns
   ([tachyon-beep — rng-isolation-patterns.md](https://github.com/tachyon-beep/skillpacks/blob/c284720d484f61e7399699c53f566746f289e1c6/plugins/axiom-determinism-and-replay/skills/using-determinism-and-replay/rng-isolation-patterns.md#1));
   player-visible seed control across shipped tactics games
   ([Steam — XCOM 2 Generate Random Seed?](https://steamcommunity.com/app/268500/discussions/0/565867915156394940?l=polish),
   [Slitherine — same dice rolls each time](https://www.slitherine.com/forum/viewtopic.php?p=539311&sid=1caeeb537a677563f153aced2f7bf1c2#p539311));
   `[motivation: INFER]` for the "one stream per subsystem" rule specifically.
4. **Canonical, order-stable state serialization + state hash** (stable field order,
   integer/fixed-point encoding, no pointer/hashmap iteration order, no raw float bytes).
   *Motivation:* `[motivation: INFER]` from rollback state capture
   ([gregorik/Rollback-Core](https://github.com/gregorik/Rollback-Core#1)),
   serialization ADRs ([opencombatengine ADR 0059](https://github.com/jamesplotts/opencombatengine/blob/main/docs/adr/0059-combat-serialization.md#1));
   **`[UNVERIFIED]`** as a directly-sourced requirement for TBS.

### Action economy and reactions

5. **An explicit, data-defined action-economy model** (AP pool / move+action / typed slots)
   with per-verb costs declared in content, and a UI-visible cost readout.
   *Motivation:* XCOM 2 players cannot tell 1-point from 2-point verbs
   ([Steam — Never Clear on what costs 1 or 2 action points](https://steamcommunity.com/app/268500/discussions/0/366298942110338752?l=danish#1));
   `[TITLE]` XCOM turn model
   ([XCOM Wiki — Turn (XCOM 2)](https://xcom.fandom.com/wiki/Turn_(XCOM_2)?m=3&sh=null__9023&utm_campaign=h2s));
   typed slots in 5e
   ([D&D Beyond — Actions vs. Bonus Actions](https://www.dndbeyond.com/forums/dungeons-dragons-discussion/rules-game-mechanics/106359-actions-vs-bonus-actions#c6#1));
   a budgeted reaction slot
   ([D&D Beyond — Reactions per turn rule?](https://www.dndbeyond.com/forums/dungeons-dragons-discussion/rules-game-mechanics/54619-reactions-per-turn-rule#c4?comment=4)).
6. **A reaction/interrupt subsystem with a bounded per-round or per-turn reaction budget.**
   *Motivation:* 5e reaction wording and the one-reaction budget
   ([D&D Beyond — i dont really understand reaction](https://www.dndbeyond.com/forums/dungeons-dragons-discussion/dungeon-masters-only/146015-i-dont-really-understand-reaction#2),
   [Gaming Pedia — Can I use a reaction and a bonus action?](https://www.ncesc.com/gaming-pedia/can-i-use-a-reaction-and-a-bonus-action/));
   overwatch as a consumed stance
   ([XCOM Wiki — Overwatch](https://xcom.fandom.com/wiki/Overwatch_(XCOM:_Enemy_Unknown)?oldid=47414),
   [IGN — Overwatch and Reaction Shots](https://www.ign.com/wikis/xcom-enemy-unknown/Overwatch_and_Reaction_Shots?objectid=161399)).
7. **Zone control as a first-class query** ("which tiles are threatened, by whom, with what
   probability") that the AI and the UI can both read.
   *Motivation:* opportunity attacks / opportunity fire are area-denial rules
   ([Open5e — SRD opportunity attacks](https://api.open5e.com/v2/rules/srd_attacking_opportunity-attacks/?format=json);
   [BattleTech — opportunity fire threads](https://battletech.com/forums/index.php?topic=18911.msg426020#msg426020));
   `[motivation: INFER]` for exposing it as a queryable service rather than an
   emergent side effect.
8. **Deterministic reaction trigger with an explicit, *static* priority key** (turn-order
   index, then stable content ordinal). Never map/pointer iteration order, never
   timestamps.
   *Motivation:* MTG's structural tie-break — active player receives priority after each
   resolution
   ([CR 117.3b](https://media.wizards.com/2025/downloads/MagicCompRules%2020250606.pdf?_gl=1*z8jay6*_gcl_au*OTg4Mjc1NDg1LjE3NDUyMzcyMTU.*FPAU*OTg4Mjc1NDg1LjE3NDUyMzcyMTU.*_ga*MTkyNTQwMDExLjE3MzczOTMxMDU.*_ga_X145Z177LS*czE3NDk4NTA4MTQkbzE4NiRnMCR0MTc0OTg1MDgxNCRqNjAkbDAkaDE0MzQyMDQxODk.*_fplc*RVIzWmVTJTJGaGtxaXFDVDdLZmNKa25RRjRlSUpqcnBKMlRUNExTOSUyRnMxbDg1bHh4NUpwWW1vS1pYUXF5cVlialgzaXBndUZVWGVOSDVYcyUyQkdvWGNmTmY5bE5MWHZDY2Iyc053aDU4QWlPUVRDTXVRSGdiUmtJSklDSE45JTJCaFElM0QlM0Q.#59#7));
   formal total-ordering of simultaneous events
   ([arXiv 2105.00069](https://ar5iv.labs.arxiv.org/html/2105.00069#2));
   `[motivation: INFER]` for the "never timestamps/iteration order" prohibition.
9. **Three separated resolution mechanisms, not one event bus:**
   (a) *event modifiers* (replacement/prevention) applied before an event, ordered by an
   explicit affected-party choice procedure; (b) *automatic invariants* (state-based
   actions) that run uninterruptibly whenever control is about to return; (c) *triggered
   abilities* on a LIFO stack.
   *Motivation:* CR 616.1 replacement/prevention ordering
   ([MagicCompRules 20150123 CS](https://wiki.mtgjudge.cn/_media/magiccomprules_20150123_cs.pdf#41#20));
   CR 116.5 state-based actions performed as a single event
   ([MagicCompRules 20140718](https://media.wizards.com/images/magic/tcg/resources/rules/MagicCompRules_20140718.pdf#11#6));
   CR 116.2c turn-based actions
   ([MagicCompRules 20180713](https://media.wizards.com/2018/downloads/MagicCompRules%2020180713.pdf#14#6));
   CR 405.6d special actions bypass the stack
   ([MagicCompRules 20190503](https://media.wizards.com/2019/downloads/MagicCompRules%2020190503.docx#51#14));
   `[motivation: INFER]` for the "three mechanisms" synthesis.
10. **Reaction resolution is non-reentrant: reactions triggered *inside* a resolution are
    queued, not invoked, and are drained only when the resolution stack is empty.**
    *Motivation:* deferred-event design in a mainstream engine
    ([Roblox — Deferred engine events](https://raw.githubusercontent.com/Roblox/creator-docs/refs/heads/main/content/en-us/scripting/events/deferred.md#1));
    the need for an "improved event recursion solution" in a shipped library
    ([ModiBuff issue #13](https://github.com/Chillu1/ModiBuff/issues/13));
    `[motivation: INFER]` for the drained-at-stack-empty boundary. **No source states this
    as a rule for games.**
11. **Triggers keyed on state *transitions*, so a continuously-true predicate cannot
    retrigger, and a per-resolution retrigger guard.**
    *Motivation:* MTG's "becomes tapped" trigger only fires on an actual untapped→tapped
    transition
    ([MagicCompRules 20160116 CS](https://wiki.mtgjudge.cn/_media/magiccomprules_20160116_cs.docx?rev=1453179506#48#18)).
12. **Loop/non-termination detection with a defined resolution** (draw, cap, or flagged
    error) rather than stack overflow.
    *Motivation:* MTG mandatory-loop draw rule and judge discussion of non-repeating loops
    ([Magic Judges Forum — loop where the game does not "return to the same state"](https://apps.magicjudges.org/forum/topic/5718/));
    `[UNVERIFIED]` at rule-number level.
13. **Delayed/expiring triggers as first-class objects with explicit provenance** (created
    by X, owned by X, destroyed with X).
    *Motivation:* CR 603.7d delayed triggered ability source rule
    ([MagicCompRules 20190503](https://media.wizards.com/2019/downloads/MagicCompRules%2020190503.docx#51#19));
    CR 403.4 a permanent that re-enters is a new object with no relation to the previous one
    ([MagicCompRules 20140718](https://media.wizards.com/images/magic/tcg/resources/rules/MagicCompRules_20140718.docx#45#13)).
14. **Continuous/static effects evaluated as a derived layer, not as queued events.**
    *Motivation:* `[motivation: INFER]` from the recurring confusion that static abilities
    do not use the stack
    ([Gaming Pedia — Do static abilities go on the stack?](https://www.ncesc.com/gaming-pedia/do-static-abilities-go-on-the-stack/#1)).
15. **Target/choice legality re-validation at resolution time**, and support for choices
    made during resolution (not just at declaration).
    *Motivation:* CR 608.2b target legality check
    ([MagicCompRules 20140201](https://media.wizards.com/images/magic/tcg/resources/rules/MagicCompRules_20140201.pdf#21#17));
    CR 608.2d choices during resolution
    ([MagicCompRules IT](https://blogs.magicjudges.org/translatedrules/files/2019/01/MagicCompRules_2020190125_IT.pdf#31#27));
    player disputes over sacrifice ordering
    ([RarityGuide — Barter in Blood ordering](http://rarityguide.com/forums/magic-rules-questions/15590206-barter-blood-what-order-creatures-sacrificed-print.html)).
16. **Multi-instruction effects execute sequentially in authored order**, with per-verb
    unit tests for order dependence.
    *Motivation:* CR worked examples for multi-part instructions
    ([MagicCompRules 20140718](https://media.wizards.com/images/magic/tcg/resources/rules/MagicCompRules_20140718.docx#45#19),
    [MagicCompRules 20240206](https://media.wizards.com/2024/downloads/MagicCompRules%2020240206.docx#64#25)).

### Replay, undo, save

17. **Command/order log as the unit of simulation input, plus periodic full snapshots**;
    the hybrid is the reference architecture.
    *Motivation:* Game Programming Patterns (command/state chapters)
    ([state](https://raw.githubusercontent.com/munificent/game-programming-patterns/911cb7606937d890c774d5503cb712b22bf1b08b/html/state.html#1),
    [data locality](https://raw.githubusercontent.com/munificent/game-programming-patterns/911cb7606937d890c774d5503cb712b22bf1b08b/html/data-locality.html#1));
    "Deterministic Command Engine – Undo, Redo & Replayable Gameplay"
    ([Unity Asset Store](https://assetstore-fallback.unity.com/packages/tools/game-toolkits/deterministic-command-engine-undo-redo-replayable-gameplay-368060));
    snapshot-within-recording prior art
    ([GB2400199A](http://patentimages.storage.googleapis.com/6b/be/33/53801525020547/GB2400199A.pdf#2#2)),
    ([US9545576B2](https://patents.google.com/patent/US9545576B2/en#7)),
    ([US20140194211A1](https://patentimages.storage.googleapis.com/14/32/96/9ef333c054f83f/US20140194211A1.pdf#6#5)).
18. **A versioned, self-describing state schema with migration**, because async matches
    outlive patches.
    *Motivation:* `[motivation: INFER]` from async TBS products
    ([brainCloud Async Match](https://docs.braincloudservers.com/5.2.0/api/capi/asyncmatch/#api-summary),
    [Beamable async turn notifications](https://docs.beamable.com/docs/asynchronous-turn-based-notifications),
    [Civ VI Play By Cloud](https://steamcommunity.com/app/289070/discussions/4/3247562523077471279?l=french))
    and from serialization ADRs
    ([opencombatengine ADR 0059](https://github.com/jamesplotts/opencombatengine/blob/main/docs/adr/0059-combat-serialization.md#1)).
    **`[UNVERIFIED]`** as a directly-sourced requirement.
19. **Undo implemented as *state restore*, not as inverse commands.** Stateful derived
    subsystems (fires, smoke, hazards, line-of-sight caches) must be restored too.
    *Motivation:* the shipped *Into the Breach* bug where "[Undo move] button can make
    smoke disappear"
    ([Subset Games forum](https://subsetgames.com/forum/viewtopic.php?p=118198&sid=7dcc2c06ecfb6fcb3fea10ce5da3aa8e#p118198));
    Into the Breach's design postmortem
    ([GDC Vault](https://gdcvault.com/play/1026333/-Into-the-Breach-Design#1#1)).
20. **Replay that can be viewed with a *presentation* order distinct from the simulation
    order** (pacing/chronology is a UX concern).
    *Motivation:* Old World's "same chronological order for events damages replayability"
    ([Steam — Old World feedback](https://steamcommunity.com/app/597180/discussions/1/4625855423768032153/#1));
    replay/replayability in Soren Johnson's GDC 2022 postmortem
    ([GamesPress](https://www.gamespress.com/es/Soren-Johnson-GDC-2022-Talk#1)).
21. **Turn-state authority handoff for hotseat/pass-and-play**, with hide-information
    screens between players.
    *Motivation:* Hotseating as a named design pattern
    ([ITU Chalmers — Hotseating](http://virt10.itu.chalmers.se/index.php?title=Hotseating&oldid=14852&printable=yes)).
22. **Replay-based validity checking** (the authority re-simulates a client's command batch
    and rejects divergence) — also the anti-cheat mechanism.
    *Motivation:* replay-past-inputs restore prior art
    ([US9545576B2](https://patents.google.com/patent/US9545576B2/en#7));
    authoritative server model
    ([Nakama — Authoritative Multiplayer](https://heroiclabs.com/docs/nakama/concepts/multiplayer/authoritative/));
    `[motivation: INFER]` for it being the anti-cheat mechanism.

### Networking

23. **Selectable authority model** — server-authoritative (default for online TBS),
    hotseat/local, and optionally peer lockstep — behind one simulation interface.
    *Motivation:* authoritative multiplayer as a documented server model
    ([Nakama](https://heroiclabs.com/docs/nakama/concepts/multiplayer/authoritative/),
    [Nakama server-authoritative doc](https://raw.githubusercontent.com/heroiclabs/nakama-docs/refs/heads/master/docs/nakama/concepts/server-authoritative-multiplayer.md#1));
    client-authoritative contrast
    ([ursina networking docs](https://raw.githubusercontent.com/pokepetter/ursina/master/docs/networking.html#Client%20Side%20Prediction#2));
    lockstep as the peer alternative
    ([Gaffer On Games — Deterministic Lockstep](https://www.gafferongames.com/post/deterministic_lockstep/));
    one engine exposing lockstep *and* rollback as scenarios
    ([fortress-rollback User Guide](https://github.com/wallstop/fortress-rollback/wiki/User-Guide#network-scenario-configuration-guide#2)).
24. **Specified, documented order of command application and event delivery** on the
    network boundary.
    *Motivation:* Unreal documents replicated-object execution order as a specified
    contract
    ([Epic — Replicated Object Execution Order](https://dev.epicgames.com/documentation/unreal-engine/replicated-object-execution-order-in-unreal-engine?application_version=5.7)).
25. **Turn submission as a durable, idempotent transaction** (submit → validate → commit →
    notify), surviving disconnects and re-submissions.
    *Motivation:* async-match APIs exist as a productised feature
    ([brainCloud Async Match](https://docs.braincloudservers.com/5.2.0/api/capi/asyncmatch/#api-summary));
    "your turn" notifications as infrastructure
    ([Beamable — Asynchronous Turn Based Notifications](https://docs.beamable.com/docs/asynchronous-turn-based-notifications));
    reconnection patents ([CN111416849A](https://patentimages.storage.googleapis.com/6e/b6/19/068afe2c1a550c/CN111416849A.pdf#5#3)).
26. **Off-turn / correspondence mode with persisted match state and push or platform
    notification on turn change.**
    *Motivation:* Civ VI Play By Cloud
    ([Steam announcement](https://steamcommunity.com/app/289070/discussions/4/3247562523077471279?l=french));
    Hero Academy as the commercial async-tactics case
    ([PocketGamer](https://www.pocketgamer.com/hero-academy/ex-age-of-employees-team-releases-first-mobile-title-ios-tactics-game-hero-academy/),
    [RPS](https://www.rockpapershotgun.com/hero-academy-opening-on-steam-august-8th));
    Beamable notifications
    ([docs](https://docs.beamable.com/docs/asynchronous-turn-based-notifications));
    PBEM demand in a mature engine
    ([vcmi issue #5599](https://github.com/vcmi/vcmi/issues/5599#1)).
27. **Desync detection (state hash exchange at turn boundaries) and resync by snapshot
    transfer.**
    *Motivation:* `[motivation: INFER]` from rollback state capture
    ([gregorik/Rollback-Core](https://github.com/gregorik/Rollback-Core#1));
    **`[UNVERIFIED]`** — I found no practitioner writeup on TBS desync detection.
28. **Per-match content/rules version pinning** so a long-running async match is unaffected
    by a balance patch.
    *Motivation:* `[motivation: INFER]` from the combination of async match lifetimes
    ([brainCloud](https://docs.braincloudservers.com/5.2.0/api/capi/asyncmatch/#api-summary))
    and command-log replay (Topic B4). **`[UNVERIFIED]`**.
29. **Rollback netcode deliberately *not* implemented by default**, with the decision
    documented and the determinism/snapshot prerequisites still built (they are needed for
    undo, replay, and desync detection anyway).
    *Motivation:* rollback's true cost drivers are documented by its implementations —
    fixed-step sim, reflection-based state capture, input redundancy, ACKs
    ([gregorik/Rollback-Core](https://github.com/gregorik/Rollback-Core#1));
    GGPO's scope
    ([GGPO](https://www.ggpo.net/),
    [GGPO README](https://raw.githubusercontent.com/pond3r/ggpo/master/doc/README.md#1));
    prediction as a separately configurable layer
    ([lightyear PredictionConfig](https://docs.rs/lightyear/0.17.0/lightyear/client/prediction/plugin/struct.PredictionConfig.html#1)).
    `[motivation: INFER]` for the "skip rollback for TBS" conclusion — **no source states
    it**.
30. **Simultaneous-turn (WEGO) support as an alternative turn-resolution mode** if the
    design needs it.
    *Motivation:* Frozen Synapse
    ([Wikipedia](https://en.wikipedia.org/wiki/Frozen_Synapse_Prime#1),
    [RPS preview](https://www.rockpapershotgun.com/preview-frozen-synapse#1));
    BattleTech's phased initiative/opportunity fire
    ([Alpha Strike Quick-Start Rules](https://battletech.com/wp-content/uploads/2017/05/AlphaStrikeQuick-Start-Rules.pdf#7#4));
    `[motivation: INFER]` for the "WEGO is the case where rollback-like machinery becomes
    relevant" claim.

---

## 3. Source-quality flags

*(Reproduced from lines 1077–1149.)*

**Primary / authoritative (still unread by me — title-level only):**

- Magic: The Gathering Comprehensive Rules, Wizards of the Coast
  ([2014 DOCX](https://media.wizards.com/images/magic/tcg/resources/rules/MagicCompRules_20140718.docx),
  [2014 PDF](https://media.wizards.com/images/magic/tcg/resources/rules/MagicCompRules_20140718.pdf#11#6),
  [2018 PDF](https://media.wizards.com/2018/downloads/MagicCompRules%2020180713.pdf#14#6),
  [2019 DOCX](https://media.wizards.com/2019/downloads/MagicCompRules%2020190503.docx#51#19),
  [2024 DOCX](https://media.wizards.com/2024/downloads/MagicCompRules%2020240206.docx#64#18),
  [2025 PDF](https://media.wizards.com/2025/downloads/MagicCompRules%2020250606.pdf?_gl=1*z8jay6*_gcl_au*OTg4Mjc1NDg1LjE3NDUyMzcyMTU.*FPAU*OTg4Mjc1NDg1LjE3NDUyMzcyMTU.*_ga*MTkyNTQwMDExLjE3MzczOTMxMDU.*_ga_X145Z177LS*czE3NDk4NTA4MTQkbzE4NiRnMCR0MTc0OTg1MDgxNCRqNjAkbDAkaDE0MzQyMDQxODk.*_fplc*RVIzWmVTJTJGaGtxaXFDVDdLZmNKa25RRjRlSUpqcnBKMlRUNExTOSUyRnMxbDg1bHh4NUpwWW1vS1pYUXF5cVlialgzaXBndUZVWGVOSDVYcyUyQkdvWGNmTmY5bE5MWHZDY2Iyc053aDU4QWlPUVRDTXVRSGdiUmtJSklDSE45JTJCaFElM0QlM0Q.#59#7))
  — note the 2025 URL is the raw returned URL with tracking parameters; it is quoted
  verbatim rather than cleaned.
- [Open5e SRD API — opportunity attacks](https://api.open5e.com/v2/rules/srd_attacking_opportunity-attacks/?format=json)
- [GGPO](https://www.ggpo.net/) and [GGPO README](https://raw.githubusercontent.com/pond3r/ggpo/master/doc/README.md#1)
- [Gaffer On Games](https://www.gafferongames.com/post/deterministic_lockstep/) (Fiedler) — well-regarded practitioner reference
- [Gamasutra AoE article (WPI mirror)](http://web.cs.wpi.edu/~claypool/courses/4513-B03/papers/games/aoe.pdf#2#1) — historically authoritative
- Vendor docs: [Nakama](https://heroiclabs.com/docs/nakama/concepts/multiplayer/authoritative/),
  [Photon](https://doc.photonengine.com/zh-cn/quantum/v3/manual/quantum-ecs/fixed-point),
  [Steamworks](https://partner.steamgames.com/doc/features/multiplayer/matchmaking?l=japanese#1),
  [PlayFab](https://learn.microsoft.com/hu-hu/xbox/playfab/community/associations/groups/using-shared-group-data#1),
  [brainCloud](https://docs.braincloudservers.com/5.2.0/api/capi/asyncmatch/#api-summary),
  [Beamable](https://docs.beamable.com/docs/asynchronous-turn-based-notifications),
  [Colyseus](https://0-16-x.docs.colyseus.io/examples/unity/turn-based-tanks)
- GDC Vault talk pages: [For Honor determinism](https://www.gdcvault.com/play/1026322/),
  [For Honor deterministic vs replicated AI](https://www.gdcvault.com/play/1024454/),
  [Into the Breach postmortem](https://gdcvault.com/play/1026333/-Into-the-Breach-Design#1#1),
  [AoS: Realms of Ruin cross-platform determinism](https://gdcvault.com/play/1034229/Cross-Platform-Determinism-in-Warhammer)
- Academic: [arXiv 2105.00069](https://ar5iv.labs.arxiv.org/html/2105.00069#2),
  [arXiv 2104.06262](https://export.arxiv.org/pdf/2104.06262#5#2),
  [UNR course notes](https://www.cse.unr.edu/~sushil/class/381/notes/gameNetworking.pdf#1#1)
- [PCG — multiple streams](https://pcg-random.org/useful-features.html#multiple-streams-sequences)
- [Game Programming Patterns](https://raw.githubusercontent.com/munificent/game-programming-patterns/911cb7606937d890c774d5503cb712b22bf1b08b/html/state.html#1) (Nystrom)
- [ITU Chalmers game design patterns — Hotseating](http://virt10.itu.chalmers.se/index.php?title=Hotseating&oldid=14852&printable=yes)

**Weak sources — used only for existence/motivation, never for a rule claim:**

- "Gaming Pedia" (ncesc.com) SEO pages:
  [overwatch penalty](https://www.ncesc.com/gaming-pedia/what-is-the-penalty-for-overwatch-in-xcom-2/#1),
  [reaction and bonus action](https://www.ncesc.com/gaming-pedia/can-i-use-a-reaction-and-a-bonus-action/#1),
  [reaction part of turn](https://www.ncesc.com/gaming-pedia/is-a-reaction-a-part-of-your-turn/#1),
  [rule 603.6](https://www.ncesc.com/gaming-pedia/what-is-the-rule-603-6-in-magic/#1),
  [static abilities on the stack](https://www.ncesc.com/gaming-pedia/do-static-abilities-go-on-the-stack/#1),
  [spells on the stack](https://www.ncesc.com/gaming-pedia/what-are-spells-on-the-stack/#1)
- Destructoid SEO/explainer pages ([stack](https://www.destructoid.com/the-stack-in-mtg-explained/),
  [phases](https://www.destructoid.com/all-mtg-turn-phases-and-steps-explained/))
- Fextralife forum ([Ever Vigilant](https://fextralife.com/forums/viewtopic.php?f=181&t=534507))
- Steam Community threads, GameFAQs, Matrix/Slitherine forums, Whirlpool forums,
  Reddit-adjacent mirrors: anecdotal player reports
- Unity Asset Store / npm / NuGet listings: commercial or unvetted
- Patent filings (GB2400199A, US9545576B2, US20140194211A1, WO2014109880A1, CN111416849A,
  US6884172, US20170024342A1): good *prior-art* evidence of a technique existing, poor
  design guidance, and legally framed
- `tachyon-beep/skillpacks` and `thedivergentai/gd-agentic-skills`: AI-generated skill-pack
  repositories found by search; on-topic but not authored authority
- `its-not-rocket-science/ananke`, `JavierIslas/Card-Combat-System`,
  `theUniC/echomancy`, `Card-Forge/forge`, `jamesplotts/opencombatengine`: useful
  open-source reference implementations, but individually unvetted
- The `void-framework-paper6.pdf` on Zenodo and `/hf3fs-jd/...` local dataset paths that
  appeared in results: **not citable** — the latter is not a public URL and the former is
  an unvetted preprint surfaced by keyword collision, not relevance

**Result noise to ignore:** several searches returned unrelated material because of
keyword collisions — a Colombian restaurant health inspection
([oregonlive](https://restaurants.oregonlive.com/inspections/backwoods-brewing-company-cman-azpkpa/DTAA-B2JSSC)),
a Chinese stock-image site ([huitu.com](https://www.huitu.com/design/show/20170716/162356261050.html)),
an OpenBSD ports mailing list thread
([marc.info](https://marc.info/?l=openbsd-ports&m=172236794016616&w=2#1)), a Columbia
student project report
([cs.columbia.edu](https://www.cs.columbia.edu/%7Esedwards/classes/2025/4840-spring/reports/Rhythm-Master-report.pdf#9#7)),
and a VMware/OSDI proceedings PDF
([usenix.org](https://static.usenix.org/events/osdi10/tech/full_papers/osdi10_proceedings.pdf#129#35)).
None of these inform the topic.

---

## 4. Highest-value next reads (when network access is available)

*(Reproduced from lines 1153 onward. The list was ordered by how much of the document was
`[UNVERIFIED]`.)*

1. **MTG Comprehensive Rules (current)** — read sections 1 (game concepts/APNAP), 4
   (zones), 5 (turn structure), 6 (spells/abilities/effects, incl. the stack), 7
   (additional rules), and especially **603 (triggered abilities)**, **608.2 (resolving)**,
   **613/614/616 (continuous, replacement, prevention)**, **704 (state-based actions)**.
   This single document resolves most of Topic A's uncertainty.
2. **Gaffer On Games: Deterministic Lockstep + Floating Point Determinism** — three short
   posts; resolves most of Topic B's `[UNVERIFIED]` items.
3. **The Gamasutra "1500 Archers" article** (the WPI mirror) — the historical lockstep
   reference.

*(The original list continued past item 3; I did not read its remaining entries before the file was
destroyed. The consolidated document's own "highest-value reads" list — also in
`tbs-gap-analysis-research.md` — covers the same ground in 12 items and should be used instead.)*

---

## 5. What is permanently lost from this file

Stated plainly so nobody assumes it is recoverable from here:

- **The entire body of Topics A, B and C** (lines ~44–819): the per-claim narrative, and in
  particular **the specific URLs for each of the ~362 sourced links** the original contained.
  §1 above preserves the headings, the summary claims, and the top-line source names, but **not the
  citations**.
- **`§0` (methodology/evidence grading)** beyond the method note reproduced above.
- **The tail of §4 (next reads, items 4+).**

**Mitigation:** the consolidated document `tbs-gap-analysis-research.md` independently covers all
three topics (its §2.2 action economy/reactions, §2.3 determinism/replay/rollback, §2.7 networking)
with its own citation set, and its "Confidence and gaps" section carries the
rollback-is-inference-only and desync-writeup-gap findings. Where the two documents agreed, nothing
was lost. Where only `t2b` had the citation, it is gone.

**Recommendation:** if the finer determinism/reaction citations are needed, re-research that
quadrant — with a working fetch path plus ~25 targeted searches it can be rebuilt from scratch in
about the same effort as reading this notice.
