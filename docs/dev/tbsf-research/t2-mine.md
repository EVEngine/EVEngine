# TARGET 2 — Turn-based strategy / tactics architecture (engine-agnostic)

> **Provenance note:** this is one of two Target 2 working documents. It is the version written
> directly by the lead agent. A parallel, independent Target 2 workstream produced
> `t2a-turn-scheduling.md`, `t2b-determinism-networking.md` and `t2c-ai-grid-fog-persistence.md`.
> Where this file and those disagree, prefer the specialist file for its topic — it had more search
> budget.
>
> **Retrieval limitation (applies to every claim):** `web_fetch` was non-functional in this session
> (every URL → *"URL hostname resolves to a non-public IP address"*) and direct HTTP from the shell
> also failed. The only channel was `web_search`, which returns titles/URLs but not page bodies.
> **No cited page was opened.**

## Legend used throughout

- ✅ **[SNIPPET]** — the claim's text was visible in a search result title/snippet for that URL.
- 📄 **[TITLE]** — the URL and its title confirm the page covers this topic; the body was not read, so the *detail* is my summary and may not match the author's exact wording.
- ⚠️ **[WEAK]** — title-level only and the source is weak (forum, aggregator, marketplace listing, AI-generated repo).
- 🧠 **[INFER]** — from my own domain knowledge. Plausible and standard, but **not verified in this session**. The linked source is the standard reference for the mechanism, not a citation of a sentence I read.
- ❌ **[UNVERIFIED]** — could not confirm at all.

---

## 1. Turn scheduling patterns

### 1.1 The design space
- The canonical taxonomy of turn-order mechanisms is **"Turn Order and Structure"**, chapter 2 of *Building Blocks of Tabletop Game Design* (Engelstein & Shalev) — 📄 [Taylor & Francis chapter listing](https://www.taylorfrancis.com/chapters/mono/10.1201/9780429430701-2/turn-order-structure-geoffrey-engelstein-isaac-shalev). The same book's "Area Control" chapter covers zone-of-control-style board influence — 📄 [Taylor & Francis](https://www.taylorfrancis.com/chapters/mono/10.1201/9780429430701-11/area-control-geoffrey-engelstein-isaac-shalev).
- The Chalmers/ITU **game design patterns** catalogue has a dedicated **"Turn Taking"** pattern — 📄 [virt10.itu.chalmers.se — Turn Taking](http://virt10.itu.chalmers.se/index.php?title=Turn_Taking&direction=prev&oldid=6070&printable=yes) — and a separate **"Hotseating"** pattern for pass-and-play — 📄 [virt10.itu.chalmers.se — Hotseating](http://virt10.itu.chalmers.se/index.php?title=Hotseating&direction=prev&oldid=14857&printable=yes).
- Wargaming has its own formal vocabulary for this (including WEGO vs IGOUGO); the **MORS *Compendium of Wargaming Terms*** is the reference — 📄 [MORS PDF](https://www.mors.org/Portals/87/Documents/Communities/Wargaming-CoP/2020-A-Compendium-of-Wargaming-Terms-8-July-2018.pdf).

### 1.2 Side-alternating / IGOUGO
- 🧠 **[INFER]** The default model: a player takes *all* their units' actions, then the opponent does. *Advance Wars* and *Fire Emblem* are the archetypes (player phase / enemy phase); *Civilization* is IGOUGO at the empire level.
- 📄 [virt10 — Turn Taking](http://virt10.itu.chalmers.se/index.php?title=Turn_Taking&direction=prev&oldid=6070&printable=yes) is the pattern reference.

### 1.3 Initiative / CTB (Charge Time Battle)
- 🧠 **[INFER]** *Final Fantasy Tactics* uses **Charge Time (CT)**: every unit's CT accumulates by its **Speed** each tick; when CT crosses a threshold the unit acts and CT resets to 0. Because the gauge is global, fast units can act twice before a slow unit acts once, and predicted turn order can be displayed.
- 📄 [Game8 — "CT (Charge Time) Explained" (FFT)](https://game8.co/games/Final-Fantasy-Tactics/archives/543470) confirms the mechanism and its name.
- 📄 [FFHacktics — "Charge Times (Average Turn Time)"](https://ffhacktics.com/smf/index.php?topic=4384.0) is a reverse-engineering community thread on the exact CT accumulation formula (the authoritative *number-crunching* source for FFT).
- 🧠 **[INFER]** *Final Fantasy X* replaced ATB with a fully deterministic **Conditional Turn-Based Battle (CTB)** system with a visible turn queue.
- 📄 Related history: [Hardcore Gamer — The Evolution of Final Fantasy Battle Systems, Part 2](https://hardcoregamer.com/features/the-evolution-of-final-fantasy-battle-systems-part-2-16-bit-revolution/285180/#active-time-battles-atb-final-fantasy-iv-ix).

### 1.4 ATB (Active Time Battle)
- 🧠 **[INFER]** ATB is a *real-time-ish* gauge: each combatant fills an ATB bar over wall-clock time; when full, the player issues a command (with an optional pause, "Wait" vs "Active" mode). It is closer to a pacing/UI device than a strict scheduling model, and it makes the game non-deterministic under real time unless the tick is fixed.
- 📄 [Rock Paper Shotgun — "Final Fantasy's ATB battle system was inspired by race cars"](https://www.rockpapershotgun.com/final-fantasy-ivs-active-time-battle-system-was-inspired-by-race-cars) (design-origin interview).
- 📄 [Hardcore Gamer — Evolution of FF Battle Systems, Part 2](https://hardcoregamer.com/features/the-evolution-of-final-fantasy-battle-systems-part-2-16-bit-revolution/285180/#active-time-battles-atb-final-fantasy-iv-ix).

### 1.5 Phase-based turns
- 🧠 **[INFER]** A turn is subdivided into typed phases (e.g. movement → shooting → assault), each of which iterates over the player's units. This is how tabletop *Warhammer* works and how many wargames structure a turn.
- 📄 A retrieved DTIC wargame document exposed exactly this structure as a table with **PHASE / SEGMENT / NOTES** columns (row: "1. Planning — Player Planning") — ✅ **[SNIPPET]** [DTIC AD1161430](https://apps.dtic.mil/sti/trecms/pdf/AD1161430.pdf).

### 1.6 Interleaved / alternating activation
- 🧠 **[INFER]** Rather than one player moving everything, activations alternate unit-by-unit (or squad-by-squad). This raises tactical reactivity and reduces the "my whole army died before it moved" problem.
- 📄 [FFG — Dust Warfare designer diary, part two](https://drafts.fantasyflightgames.com/en/news/2011/11/4/dust-warfare-designer-diary-part-two/) is a designer's own account of alternating activation in a miniatures wargame.
- *Into the Breach* is often cited as the extreme form of interleaving: the player sees the enemy's *telegraphed* intent for the coming turn and must position to counter it — 📄 [GDC Vault — Into the Breach Design Postmortem](https://gdcvault.com/play/1026333/-Into-the-Breach-Design) and 📄 [Game Developer — "Video: How Subset Games designed Into the Breach"](https://www.gamedeveloper.com/design/video-how-subset-games-designed-i-into-the-breach-i-).
- *XCOM 2* interleaves at the squad level and gates enemy activation on **pod activation / "hidden movement"** — 📄 [MCV — "GDC 13: Inside X-COM's hidden movement"](https://mcvuk.com/development-news/gdc-13-inside-x-coms-hidden-movement/) and 📄 [Rock Paper Shotgun — XCOM 2 guide](https://www.rockpapershotgun.com/xcom-2-guide).

### 1.7 Simultaneous resolution (WEGO)
- 🧠 **[INFER]** Both sides write orders in a planning phase; the engine then executes the whole turn simultaneously (with a replay). *Frozen Synapse* is the canonical real-time-tactics example; *Combat Mission* and *Battlestar Galactica Deadlock* are WEGO wargames.
- 📄 [Matrix Games — Frozen Synapse product page](http://www1.matrixgames.com/store/409/Frozen.Synapse?g=63&a=3) and 📄 [Slitherine — Frozen Synapse](https://www6.slitherine.com/game/frozen-synapse).
- 📄 [Matrix Games forum — "More Wego"](https://www1.matrixgames.com/forums/printable.asp?m=841795) is a designer/community discussion thread on WEGO turn resolution.
- ✅ **[SNIPPET]** A thesis on command execution exposed the sentence *"In turn 3 and 4 the first player issued commands are executed"* — 📄 [core.ac.uk PDF](https://core.ac.uk/download/612071353.pdf) — i.e. the write-orders-then-execute structure.

### 1.8 Specific-game mechanism notes
- **XCOM (1994) / UFO:AI** — time-unit (TU) action points: every action costs TUs from a per-turn pool — 📄 [UFO:AI Manual — Time Units](https://ufoai.org/w/index.php?title=Manual/Singleplayer/Time_Units/v2.5&feed=atom&action=history).
- **BattleTech (2018)** — initiative is by weight class, with phases resolving heaviest-last / lightest-first; the tabletop *BattleTech* quick-start rules are the reference for the underlying order — 📄 [battletech.com/qsr](https://battletech.com/qsr/).
- **Final Fantasy Tactics** — CT, per §1.3.
- **Fire Emblem** — strict player phase / enemy phase with counterattacks resolved inside each combat — 🧠 **[INFER]**; no primary source retrieved.
- **Civilization** — IGOUGO at the civilisational level, with a simultaneous-turn multiplayer mode — 📄 [Steam Community — Civ VI "Starting New Cloud Game Feature"](https://steamcommunity.com/app/289070/discussions/4/3247562523077471279?l=french).
- **Old World** — Soren Johnson's postmortem discusses order-of-action and AI in a turn-based 4X — 📄 [GDC Vault — "My Elephant in the Room: An 'Old World' Postmortem"](https://www.gdcvault.com/play/1028038/).

### 1.9 What scheduling implies for the runtime
- A **turn/initiative queue** with a deterministic tie-break, not just a "current player" field (CTB/initiative).
- A **phase/segment state machine** (phase-based, WEGO planning → execution).
- An **ordered action/command buffer** that can be executed, replayed and (for WEGO) previewed.
- **Interrupt/entry points** in the schedule (overwatch, reactions — §2).
- A UI-facing **predicted turn order** (games like FFT surface it).
- Sources: 📄 [Turn Order and Structure](https://www.taylorfrancis.com/chapters/mono/10.1201/9780429430701-2/turn-order-structure-geoffrey-engelstein-isaac-shalev), 📄 [virt10 — Turn Taking](http://virt10.itu.chalmers.se/index.php?title=Turn_Taking&direction=prev&oldid=6070&printable=yes), 📄 [DTIC AD1161430](https://apps.dtic.mil/sti/trecms/pdf/AD1161430.pdf).

---

## 2. Action economy, reactions and interrupts

### 2.1 Action economy shapes
- 🧠 **[INFER]** Four common shapes: (a) **action points / time units** (XCOM 1994, UFO:AI); (b) **move + action**, optionally "two moves or move+shoot" (XCOM 2); (c) **move + action + bonus action** (D&D 5e); (d) **single activation per unit per turn** with no economy at all (Into the Breach).
- 📄 [UFO:AI Manual — Time Units](https://ufoai.org/w/index.php?title=Manual/Singleplayer/Time_Units/v2.5&feed=atom&action=history) documents (a).
- 📄 [Rock Paper Shotgun — XCOM 2 guide](https://www.rockpapershotgun.com/xcom-2-guide) for (b).

### 2.2 Reaction / interrupt / opportunity systems
- 🧠 **[INFER]** Reactions need three things the base turn loop does not provide: (1) a **trigger** condition evaluated on every state change; (2) an **interrupt window** in the middle of another actor's action, where the trigger's owner can pre-empt; (3) an **ordering policy** when several triggers fire at once.
- The most rigorously specified public model of (3) is **Magic: The Gathering's stack + priority**, whose comprehensive rules define exactly when a player receives priority and in what order objects resolve. ✅ **[SNIPPET]** The retrieved rule text reads *"117.3b The active player receives priority after a spell or ability (other than a mana ability) resolves"* — [Magic Comprehensive Rules (2025-06-06)](https://media.wizards.com/2025/downloads/MagicCompRules%2020250606.pdf). An older edition is also indexed with the turn-structure rule *"116.2c Turn-based actions happen automatically when certain steps or phases begin"* — 📄 [MagicCompRules 2018-07-13](https://media.wizards.com/2018/downloads/MagicCompRules%2020180713.pdf).
- 🧠 **[INFER]** MTG's important structural properties for an engine: the stack is **LIFO**, priority is **explicitly passed**, state-based actions are checked only when a player would receive priority, and the game is **non-reentrant** by rule (you cannot "cut in" arbitrarily; you must receive priority).
- 📄 A concrete engine-side implementation of the same idea (a "stack resolution system" issue in a tactical game project) is [anchapin/planar-nexus issue #8](https://github.com/anchapin/planar-nexus/issues/8).
- **Overwatch / return fire in practice is a common pain point.** ✅ **[SNIPPET]** The Phoenix Point forums contain a thread literally titled *"Return fire fail"* — [forums.snapshotgames.com/t/return-fire-fail/11434](https://forums.snapshotgames.com/t/return-fire-fail/11434/22) — i.e. players disputing when reaction fire should and should not have triggered. A *BattleTech* forum thread discusses *"Opportunity fire in Alpha Strike"* — 📄 [bg.battletech.com forums](https://bg.battletech.com/forums/index.php/topic,78804.0/prev_next,prev.html?PHPSESSID=venonuqcp1jl0l3p1hbcuhatsb#new).
- 📄 [Game Developer — "Feature: Anatomy Of A Combat Zone"](https://www.gamedeveloper.com/game-platforms/feature-anatomy-of-a-combat-zone) is a design analysis of the combat interaction space (cover, reaction, exposure).
- 📄 [Building Blocks — Area Control](https://www.taylorfrancis.com/chapters/mono/10.1201/9780429430701-11/area-control-geoffrey-engelstein-isaac-shalev) and the MORS compendium cover **zone of control**, which is the movement-side counterpart of reactions.

### 2.3 Determinism and non-reentrancy of reactions
- 🧠 **[INFER]** Practical rules that keep a reaction system deterministic:
  1. **Single trigger queue**: reactions are *enqueued*, never executed inline from inside another reaction.
  2. **Total order on simultaneous triggers**: fixed by (phase, initiative, side, entity id) — never by hash-map iteration order.
  3. **Depth limit / once-per-window flags**: each reaction may fire at most once per interrupt window.
  4. **Resolution outside the mutation**: resolve the reaction *after* the triggering action's atomic step completes, so the world is never observed half-mutated.
  5. **No RNG inside trigger ordering**; if RNG is needed it comes from a named stream in the fixed order.
- Sources for the *order-and-priority* half of this: ✅ [MTG Comprehensive Rules](https://media.wizards.com/2025/downloads/MagicCompRules%2020250606.pdf); 📄 [anchapin/planar-nexus #8](https://github.com/anchapin/planar-nexus/issues/8).

### 2.4 What reactions imply for the runtime
- An **event/trigger bus** with deterministic subscription order.
- An explicit **interrupt window** concept in the action resolver.
- A **reaction queue with a depth cap** and once-per-window guards.
- A **priority/turn-pass** concept if reactions are player-driven (MTG-style).
- Sources: ✅ [MTG CR](https://media.wizards.com/2025/downloads/MagicCompRules%2020250606.pdf), 📄 [Phoenix Point "Return fire fail"](https://forums.snapshotgames.com/t/return-fire-fail/11434/22).

---

## 3. Determinism, replay and rollback

### 3.1 Lockstep determinism
- The classic primary-engineering write-up is the Age of Empires piece **"1500 Archers on a 28.8k Modem"**, which describes deterministic lockstep, command turns and why all clients must produce bit-identical results — 📄 [PDF mirror (WPI course copy)](http://web.cs.wpi.edu/~claypool/courses/4513-B03/papers/games/aoe.pdf).
- 📄 A Spanish-language thesis explicitly frames the technique (*"Es aquí cuando nació el Deterministic Lockstep…"*) — [burjcdigital.urjc.es](https://burjcdigital.urjc.es/server/api/core/bitstreams/6de759e4-0ebf-4d11-bb5b-12f4614d8d17/content).

### 3.2 Fixed-point vs floating point
- 🧠 **[INFER]** Floating-point results are not guaranteed identical across compilers/CPUs/SIMD paths, which breaks bit-exact lockstep. TBS engines that want lockstep or exact replays either (a) use **fixed-point** math for all simulation arithmetic, or (b) constrain FP usage (no `fast-math`, no x87 excess precision, fixed op order) and accept a tighter platform matrix.
- 📄 A retrieved HAL thesis fragment references fixed-point representation explicitly: *"More information about fixed point representation can be found at http://www…"* — 📄 [theses.hal.science/tel-00071184](https://theses.hal.science/tel-00071184v1/document).
- 📄 A UE5 rollback implementation (fixed-step simulation + state capture) is [gregorik/Rollback-Core](https://github.com/gregorik/Rollback-Core) — its own blurb states *"fixed-step simulation, SaveGame-reflection state capture, UDP transport with input redundancy and reliable ACKs"* ✅ **[SNIPPET]**.

### 3.3 Seeded RNG streams
- 🧠 **[INFER]** One global RNG shared by everything is the standard source of desyncs and irreproducible replays. The robust pattern is **named, independently seeded streams** (combat, AI, cosmetic/VFX, world-gen), each serialised into the save, with cosmetic streams explicitly excluded from simulation state.
- 📄 Ecosystem evidence that this is a recognised building block: [@opensourceframework/seeded-rng](https://www.npmjs.com/package/@opensourceframework/seeded-rng?activeTab=readme), [@zakkster/lite-random](https://www.npmjs.com/package/@zakkster/lite-random?activeTab=code).

### 3.4 Commands, event sourcing, snapshots
- 🧠 **[INFER]** Two persistence models: **command log** (store the ordered commands; re-simulate to reconstruct state — small, replayable, but fragile: any sim change invalidates old logs) vs **full state snapshot** (store the state; robust and undo-friendly, but large and needs schema migration).
- 📄 A real TBS project's architecture decision record on exactly this tradeoff: [opencombatengine ADR-0059 "Combat Serialization Strategy"](https://github.com/jamesplotts/opencombatengine/blob/main/docs/adr/0059-combat-serialization.md) and [ADR-0058 "Combat Loop Refinement"](https://github.com/jamesplotts/opencombatengine/blob/main/docs/adr/0058-combat-loop-refinement.md) ✅ **[SNIPPET — titles]**.
- 📄 A Unity Asset Store package built specifically around the command model — *"Deterministic Command Engine – Undo, Redo & Replayable Gameplay"* — shows the command-log/undo/replay triad is a productised pattern: [Asset Store page](https://assetstore-fallback.unity.com/packages/tools/game-toolkits/deterministic-command-engine-undo-redo-replayable-gameplay-368060).

### 3.5 Undo
- 🧠 **[INFER]** Undo in a TBS is cheap *only* if you already have one of: (a) full state snapshots per command (memory-heavy, trivially correct), (b) command log + re-simulation (cheap in memory, requires determinism), or (c) inverse commands (smallest, requires every command to define an exact inverse — hardest). Undo is therefore a **determinism forcing function**: it is the earliest feature that makes you commit to a determinism story.
- 📄 [Deterministic Command Engine (undo/redo/replay)](https://assetstore-fallback.unity.com/packages/tools/game-toolkits/deterministic-command-engine-undo-redo-replayable-gameplay-368060).

### 3.6 Rollback netcode and its applicability to TBS
- GGPO is the reference rollback implementation — 📄 [ggpo.net](https://www.ggpo.net/); 📄 tuning/configuration docs: [rollback-netcode/docs/configuration-tuning.md](https://github.com/someusername6/rollback-netcode/blob/HEAD/docs/configuration-tuning.md).
- 🧠 **[INFER]** Rollback exists to hide *latency* in games where input must be applied immediately (fighters, RTS). In a **turn-based** game the opponent is not acting during your turn, so latency is absorbed by the turn structure itself; rollback buys almost nothing and costs save/restore of the entire tactical state every frame. The usual TBS answer is **lockstep over commands** (or server-authoritative validation), with **state hashing** for desync detection rather than rollback.
- 📄 Desync detection and recovery as a first-class design item: [freeciv-nostr issue #18 "Phase 3.4: Desync Detection & Recovery"](https://github.com/average-gary/freeciv-nostr/issues/18).

### 3.7 What determinism implies for the runtime
- Fixed timestep / discrete tick for any timed subsystem (ATB, animations that gate logic).
- A **simulation-only RNG API** with named streams, serialised in saves.
- An **ordered command pipeline** with a stable total order.
- **State hashing** for desync detection and replay verification.
- A published **determinism contract** (what is bit-exact, what is tolerant).
- Sources: 📄 [1500 Archers](http://web.cs.wpi.edu/~claypool/courses/4513-B03/papers/games/aoe.pdf), 📄 [opencombatengine ADR-0059](https://github.com/jamesplotts/opencombatengine/blob/main/docs/adr/0059-combat-serialization.md), 📄 [Rollback-Core](https://github.com/gregorik/Rollback-Core).

---

## 4. AI for TBS

### 4.1 Choosing a technique by scale
- 🧠 **[INFER]** Rough guidance:
  - **Finite state machines / simple rules** — a handful of unit types, no planning needed.
  - **Behaviour trees** — reactive, readable, designer-authored unit behaviour; weak at long-horizon goals.
  - **Utility AI** — many heterogeneous considerations scored into one number; scales well to "many units, many options" and is easy to tune; poor at multi-step plans.
  - **HTN** — hierarchical, hand-authored decomposition into tasks; good for structured, predictable tactics; brittle when the world deviates.
  - **GOAP** — planner over action preconditions/effects; flexible and emergent; planning cost grows with action count.
  - **Minimax/alpha-beta** — strongest in small, well-defined tactical spaces (chess-like, or a single-unit local fight).
  - **MCTS** — strong where the branching factor is large but you can afford many playouts; usually too slow for a whole TBS turn, viable for local sub-problems.
  - **Influence maps** — the standard *spatial* layer under any of the above, letting hundreds of units share one cheap spatial evaluation.
- 📄 A retrieved "Architecture Selection Reference" for game AI covers the comparison explicitly — [erikhazzard/vasir — architecture_selection.md](https://raw.githubusercontent.com/erikhazzard/vasir/refs/heads/main/.agents/skills/game-ai__architecting-ai/references/architecture_selection.md).
- 📄 GOAP's origin paper is Jeff Orkin, *"Three States and a Plan: The A.I. of F.E.A.R."*; a contemporary write-up is [GameSpy — "F.E.A.R.'s AI Demystified"](http://au.gamespy.com/pc/fear/698080p1.html).
- 📄 HTN planning for games: [IJCAI abstract 15/236](https://www.ijcai.org/Abstract/15/236) and [Adversarial hierarchical-task network planning for complex real-time games (AAMAS)](https://dl.acm.org/doi/10.5555/2832415.2832479).
- 📄 Search-based TBS AI: [Evolutionary Tree Search for Turn-Based Strategy Games (ACG)](https://dl.acm.org/doi/10.1007/978-3-032-23657-9_12); 📄 minimax vs alpha-beta in a turn-based tactical RPG is studied in [ITERA repository](https://repository.itera.ac.id/depan/submission/SB2601120050).
- 📄 Influence maps: [Towards the Co-Evolution of Influence Map Tree Based Strategy Game Players (IEEE)](https://ieeexplore.ieee.org/document/4100111); 📄 [Influence Map Method based on Intransitive Relationship Information (KCI)](https://www.kci.go.kr/kciportal/ci/sereArticleSearch/ciSereArtiView.kci?sereArticleSearchBean.artiId=ART001408194).
- 📄 Terrain/spatial analysis for game AI: [Northwestern EECS 395 lecture — Terrain Analysis](https://users.cs.northwestern.edu/~forbus/395gai/lectures/L5_Terrain_Analysis.pdf).

### 4.2 Making TBS AI fast enough for many units
- 🧠 **[INFER]** Standard levers: (1) **amortise planning across frames/turns** (one unit plans per frame; a turn budget); (2) **plan once, execute many turns** (hierarchical: strategic plan now, tactical reactions later); (3) **share computation** — one influence/threat map per side instead of per unit; (4) **cheap filters before expensive evaluation** — range/AoE pre-filtering, candidate reduction; (5) **cache and invalidate** plans keyed on a world-version counter; (6) **time-box with a fallback heuristic** and record that fallback occurred.
- 📄 The Old World postmortem is the best public case study of a turn-based 4X AI under a human-visible turn budget — [GDC Vault](https://www.gdcvault.com/play/1028038/).
- 📄 Soren Johnson's classic essay on AI quality expectations in strategy games: [Game Developer — "Analysis: AI Fallibility And The Chick Parabola"](https://www.gamedeveloper.com/game-platforms/analysis-ai-fallibility-and-the-chick-parabola).
- 📄 *Into the Breach* is the notable counter-example: rather than strong search, the AI telegraphs a small set of fully deterministic attacks that the player can reason about — [GDC Vault — Into the Breach Design Postmortem](https://gdcvault.com/play/1026333/-Into-the-Breach-Design).

### 4.3 AI honesty (what the AI is allowed to know)
- 🧠 **[INFER]** TBS AI "cheating" is really a **knowledge-provenance** problem: the AI must consume the same fog-filtered view the player gets, or the design must explicitly declare where it sees more.
- 📄 There is recent work framing exactly this: *"Information Authority: A Knowledge-Provenance Architecture for Non-Cheating Tactical AI"* — 📄 [Zenodo preprint PDF](https://zenodo.org/records/20409829/files/information-authority-preprint-v1.pdf.pdf?download=1).
- 📄 General treatment of AI deception/fairness expectations: [Cell/Patterns — "AI deception: A survey"](https://www.cell.com/patterns/fulltext/S2666-3899(24)00103-X).

### 4.4 What TBS AI implies for the runtime
- The same **command API for AI and player** (AI emits commands, not direct mutations) — this is what makes AI cheap to make deterministic and replayable.
- A **fog-filtered world view** structure passed to every AI decision.
- A **threat/influence map** service shared per side.
- **Time/frame budget accounting** with an observable fallback path.
- Sources: 📄 [vasir architecture selection](https://raw.githubusercontent.com/erikhazzard/vasir/refs/heads/main/.agents/skills/game-ai__architecting-ai/references/architecture_selection.md), 📄 [Information Authority (Zenodo)](https://zenodo.org/records/20409829/files/information-authority-preprint-v1.pdf.pdf?download=1).

---

## 5. Grid / board geometry

### 5.1 Square vs hex; coordinate systems
- The canonical reference is Amit Patel's **Red Blob Games hexagonal grids guide** — 📄 [redblobgames.com/grids/hexagons/](https://www.redblobgames.com/grids/hexagons/) with a dedicated **directions** page at [redblobgames.com/grids/hexagons/directions.html](https://www.redblobgames.com/grids/hexagons/directions.html). It covers offset/axial/cube coordinates, the cube-constraint `x + y + z = 0`, hex distance, neighbour tables, line drawing and pixel↔hex conversion.
- 🧠 **[INFER]** Practical summary: **axial/cube** coordinates give constant-time distance, cheap rotation/reflection and trivial direction vectors; **offset** coordinates are needed only at the storage/UI boundary. Store axial, display offset.
- A content mirror of the same guide exists and is indexed: [zoubingwu/llm-wiki — Red Blob Games Hexagonal Grids.md](https://github.com/zoubingwu/llm-wiki/blob/master/articles/Red%20Blob%20Games%20Hexagonal%20Grids.md) — useful if the canonical site is unreachable.

### 5.2 Line of sight on grids
- 🧠 **[INFER]** Four families, with different failure modes:
  1. **Naive Bresenham line**: cheap, but **asymmetric** (A sees B ≠ B sees A) and misses cells ("holes") on shallow slopes.
  2. **Supercover / digital line**: covers *all* cells the line touches (no holes) — the fix for Bresenham holes; see [Stack Overflow — "How to find all grid squares on a line?"](https://stackoverflow.com/questions/3303936/how-to-find-all-grid-squares-on-a-line).
  3. **Permissive FOV** (roguelike lineage): a cell is visible if *any* line to it is unobstructed — generous, symmetric-ish, but expensive; reference implementation [RogueBasin — permissive FOV](https://github.com/Chizaruu/roguebasin/blob/main//wiki/permissive_field_of_view.md) and the clean-room Haskell reimplementation in [LambdaHack](https://hackage-content-origin.haskell.org/package/LambdaHack-0.4.101.1/docs/src/Game-LambdaHack-Server-Fov-Permissive.html).
  4. **Recursive shadowcasting / symmetric shadowcasting**: fast and symmetric; see [SymmetricPCVT — Symmetric Pre-Computed Visibility Tries with fast LOS](https://github.com/denismr/SymmetricPCVT) and the [rot.js FOV manual](https://cdn.jsdelivr.net/npm/rot-js@2.2.0/manual/pages/fov.html).
- 📄 Amit Patel's FOV/visibility work: [Red Blob Games — 2D Visibility](https://www.redblobgames.com/articles/visibility/) and [segment sorting](https://www.redblobgames.com/articles/visibility/segment-sorting.html) (polygon/segment-based visibility, the continuous-space alternative to grid LOS).
- 📄 [RogueBasin — computing LOS for large areas](https://github.com/Chizaruu/roguebasin/blob/53b4a47c20a6c441d75d04ed148cf7b51409e919/wiki/computing_los_for_large_areas.md); 📄 [go-fov](https://pkg.go.dev/github.com/norendren/go-fov@v1.0.1).
- 📄 Tabletop wargames are a surprisingly good source for *specified* LOS rules — e.g. a retrieved fragment from *Glory and Empire* reads *"LOS may be traced through 2 hexes of degrading terrain"* ✅ **[SNIPPET]** — [Glory and Empire core rules](https://gamers-hq.de/media/pdf/0f/22/ac/Core-Rules-1-0.pdf), with a dedicated LOS chapter at [Core Rules v1.0 Rev3](https://gamers-hq.de/media/pdf/42/76/49/Glory-and-Empire-First-Victories-Core-Rules-v1-0-Rev3.pdf).

### 5.3 Elevation / multi-level boards
- 🧠 **[INFER]** Elevation is where "2D grid" abstractions typically break: LOS must become 3D-ish (height interpolation along the ray), movement cost becomes direction-dependent, and cover is usually derived from relative height plus intervening geometry. Two implementable simplifications: (a) treat elevation as a *per-tile cost/blocking attribute* only (cheap, no height-aware LOS), or (b) store a true height per cell and do a height-aware supercover line (accurate, still grid-native).
- 📄 Terrain/spatial analysis techniques: [Northwestern EECS 395 — Terrain Analysis](https://users.cs.northwestern.edu/~forbus/395gai/lectures/L5_Terrain_Analysis.pdf).
- ❌ **[UNVERIFIED]** I did not retrieve a source that specifically documents multi-level grid LOS implementation.

### 5.4 Cover, zone of control, movement cost, pathfinding
- 📄 Cover and combat-zone design: [Game Developer — "Feature: Anatomy Of A Combat Zone"](https://www.gamedeveloper.com/game-platforms/feature-anatomy-of-a-combat-zone).
- 📄 Zone of control / area control as a tabletop mechanism: [Building Blocks of Tabletop Game Design — Area Control](https://www.taylorfrancis.com/chapters/mono/10.1201/9780429430701-11/area-control-geoffrey-engelstein-isaac-shalev); wargaming definition in the [MORS Compendium of Wargaming Terms](https://www.mors.org/Portals/87/Documents/Communities/Wargaming-CoP/2020-A-Compendium-of-Wargaming-Terms-8-July-2018.pdf).
- 🧠 **[INFER]** Movement cost + terrain give you Dijkstra/A* on a weighted graph; for TBS the useful refinements are (a) **range-limited flood fill** rather than full A* (you usually need the *set of reachable cells* plus cost, not one path), (b) **AoE/attack range as a separate shape query** (ring, cone, line, blast), (c) a **cached navigation graph** invalidated by a world-version counter.
- 📄 [Naval Postgraduate School / DTIC wargame doc](https://apps.dtic.mil/sti/trecms/pdf/AD1161430.pdf) and the [Glory and Empire rules](https://gamers-hq.de/media/pdf/0f/22/ac/Core-Rules-1-0.pdf) are examples of fully specified movement/LOS/terrain rulesets worth mining for edge cases.

---

## 6. Fog of war

- 🧠 **[INFER]** The standard three-layer model: **explored/remembered** (seen at least once), **currently visible** (seen now), **unknown** (never seen). Rendering shows live entities only where currently visible, static/remembered terrain where explored, nothing where unknown. Fog must be maintained **per player/team**, not globally.
- 📄 A student research deck explicitly on the topic: [Fog of War (FoW) — Research Project PDF](https://raw.githubusercontent.com/oscarpm5/Fog-of-War---Research-Project/master/FogOfWarppt.pdf).
- 📄 A patent describing visibility territory that "will grow and shift as the user moves their units to unexplored locations" ✅ **[SNIPPET]** — [US20240293743A1](https://patents.google.com/patent/US20240293743A1/en).
- 📄 Visibility-relevant implementation in a Finnish thesis (*"drawing the visibility data texture"*): [theseus.fi PDF](https://www.theseus.fi/bitstream/handle/10024/855628/Mannisto_Mattias.pdf?isAllowed=y&sequence=2) ✅ **[SNIPPET]**.
- **Interaction with AI fairness.** 🧠 **[INFER]** Fog is where "the AI cheats" becomes concrete: if the AI is handed the unfogged world, it will chase units it cannot see. The engine-level fix is structural, not a behaviour tweak: give the AI a **per-side filtered view object** produced by the same visibility system the UI uses, so cheating is opt-in and visible in code review.
- 📄 This exact framing is the subject of *"Information Authority: A Knowledge-Provenance Architecture for Non-Cheating Tactical AI"* — 📄 [Zenodo preprint](https://zenodo.org/records/20409829/files/information-authority-preprint-v1.pdf.pdf?download=1).
- 📄 General AI-fairness/expectation discussion: [Cell — AI deception survey](https://www.cell.com/patterns/fulltext/S2666-3899(24)00103-X).

### What fog implies for the runtime
- Per-team **visibility sets** + **explored sets**, serialised in saves.
- A **view/filter object** as the only way AI (and UI) reads the world.
- A **visibility-version counter** so AI/UI caches invalidate correctly.
- Sources: 📄 [FoW Research Project](https://raw.githubusercontent.com/oscarpm5/Fog-of-War---Research-Project/master/FogOfWarppt.pdf), 📄 [Information Authority](https://zenodo.org/records/20409829/files/information-authority-preprint-v1.pdf.pdf?download=1).

---

## 7. Networking for TBS

### 7.1 Topologies
- 🧠 **[INFER]** Four practical models:
  1. **Hotseat / pass-and-play** — one machine, alternating input. Cheapest; needs full state, no networking. 📄 [virt10 — Hotseating](http://virt10.itu.chalmers.se/index.php?title=Hotseating&direction=prev&oldid=14857&printable=yes).
  2. **Async / play-by-mail / correspondence** — turns are persisted server-side and players act whenever. Needs durable match state + notifications. 📄 [Steam — Civ VI Play By Cloud](https://steamcommunity.com/app/289070/discussions/4/3247562523077471279?l=french) and [Civ Cloud group](https://steamcommunity.com/groups/civcloud).
  3. **Lockstep over commands** — all clients apply the same ordered command stream. Simple and replay-compatible; a desync is catastrophic without hashing.
  4. **Server-authoritative** — clients submit intents, server resolves. Removes most desync/cheat classes at the cost of a server and latency on every action.
- 📄 Nakama (the backend the TBSF asset's own client targets — see `t1-tbsf-unity.md` §1.4) documents both a general multiplayer model and specifically an **authoritative** multiplayer model: [heroiclabs.com — Multiplayer Engine](https://heroiclabs.com/docs/nakama/concepts/multiplayer/) and [heroiclabs.com — Authoritative Multiplayer](https://heroiclabs.com/docs/nakama/concepts/multiplayer/authoritative/).

### 7.2 Desync avoidance and recovery
- 🧠 **[INFER]** The essentials: (a) hash simulation state at a fixed boundary (end of turn) and compare across clients; (b) never let presentation-layer RNG into simulation state; (c) version the ruleset and reject a match where clients differ; (d) on divergence, **resync by full state transfer** rather than trying to repair incrementally.
- 📄 Desync detection/recovery treated as an explicit milestone: [freeciv-nostr issue #18](https://github.com/average-gary/freeciv-nostr/issues/18).
- 📄 Rollback machinery (if you did want it): [ggpo.net](https://www.ggpo.net/), [configuration tuning](https://github.com/someusername6/rollback-netcode/blob/HEAD/docs/configuration-tuning.md).

### 7.3 What networking implies for the runtime
- A **serialisable command/intent** representation that is stable across versions.
- **Full state serialisation** (mandatory for async/hotseat/resync).
- A **state hash** function over the deterministic simulation subset.
- A **ruleset/content version id** carried in the match.
- Sources: 📄 [Nakama multiplayer](https://heroiclabs.com/docs/nakama/concepts/multiplayer/), 📄 [Nakama authoritative](https://heroiclabs.com/docs/nakama/concepts/multiplayer/authoritative/), 📄 [freeciv-nostr #18](https://github.com/average-gary/freeciv-nostr/issues/18).

---

## 8. Persistence

### 8.1 Save vs replay: the real decision
- 🧠 **[INFER]** Full-state serialisation and command-log replay are not interchangeable:
  - **Full state** — must serialise *everything* that affects future behaviour (RNG stream positions, pending reaction queues, AI plan caches, visibility/explored sets, turn-schedule state), or loading produces a subtly different game. It is large and version-fragile, but it survives sim refactors and enables undo/async/network resync.
  - **Command log** — tiny, gives replay and spectating for free, but *any* change to simulation code invalidates every old log, and it cannot represent "state that existed before the log" (so you need a periodic keyframe snapshot anyway). It also cannot support mid-battle save without a snapshot base.
- 📄 A concrete TBS project weighing exactly this: [opencombatengine ADR-0059 — Combat Serialization Strategy](https://github.com/jamesplotts/opencombatengine/blob/main/docs/adr/0059-combat-serialization.md) ✅ **[SNIPPET — title/docs]**, alongside [ADR-0058 — Combat Loop Refinement](https://github.com/jamesplotts/opencombatengine/blob/main/docs/adr/0058-combat-loop-refinement.md).
- 📄 The command-model productisation of the same idea: *"Deterministic Command Engine – Undo, Redo & Replayable Gameplay"* — [Asset Store](https://assetstore-fallback.unity.com/packages/tools/game-toolkits/deterministic-command-engine-undo-redo-replayable-gameplay-368060).

### 8.2 Mid-battle save
- 🧠 **[INFER]** A "save in the middle of a tactical battle" requirement immediately forces the engine to be able to **suspend and resume an in-flight action sequence**: the pending queue, the reaction queue, animation state, and any partially-resolved action must all be representable as data. Engines that resolve actions as a synchronous, non-interruptible call stack cannot do this without leaking.
- 📄 [opencombatengine ADR-0058 / ADR-0059](https://github.com/jamesplotts/opencombatengine/blob/main/docs/adr/0058-combat-loop-refinement.md).

### 8.3 Schema versioning and migration
- 🧠 **[INFER]** For a live game, every save needs a **schema id + version**, an explicit **unknown-field policy** (reject / ignore / preserve), and a **migration chain** (v1→v2→v3, applied in order, tested). Migration must be atomic: a failed migration must not leave partially-loaded observable state.
- 📄 A practical write-up of UE5 save versioning (the same problem in another engine): [StraySpark — "UE5 Save System Versioning: How to Not Break Save Files on Update"](https://www.strayspark.studio/blog/ue5-save-system-versioning-2026).
- 📄 Related: [idle_save (versioned save package)](https://pub.dev/packages/idle_save/versions/0.3.1).

### 8.4 What persistence implies for the runtime
- A **serialisation layer with schema id + version + migration hooks**.
- **Keyframe + delta** or **full snapshot** strategy, decided explicitly.
- **Serialisation of RNG stream state**, pending queues, and AI caches.
- A **save/load round-trip test** that asserts deep equality of the simulation state hash.
- Sources: 📄 [opencombatengine ADR-0059](https://github.com/jamesplotts/opencombatengine/blob/main/docs/adr/0059-combat-serialization.md), 📄 [StraySpark UE5 save versioning](https://www.strayspark.studio/blog/ue5-save-system-versioning-2026).
