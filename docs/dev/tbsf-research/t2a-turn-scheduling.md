# T2a — Turn Scheduling / Turn-Order Architecture in Turn-Based Strategy & Tactics Games

Engine-agnostic design-pattern survey for the EVEngine TBS gap analysis.

---

## 0. Method, channel constraints, and evidence grading (read this first)

**Channel constraint.** In this session `web_fetch` is broken (every URL fails with
`URL hostname resolves to a non-public IP address`) and direct HTTP from pwsh/curl also
fails (no network / TLS credential errors). The **only** research channel was the
`web_search` tool. That tool returned, for every query, a list of **source URLs plus
result titles**; occasionally a title carried a content fragment. **No page body was
ever retrieved.** ~35 search calls / ~90 queries were run.

**Therefore the evidence grade of almost everything below is limited.** Every claim is
tagged:

| Tag | Meaning |
|---|---|
| **[S]** | The claim is literally stated in a result **title or snippet fragment** returned by `web_search`. The page itself was **not opened**. |
| **[S-]** | Weak `[S]`: the claim is only *implied* by the existence/title of a page (e.g. "a page named *Turn (XCOM 2)* exists on a wiki"), not stated outright. |
| **[I]** | **Inference / background model knowledge**, NOT verified in this session from any retrieved source. Treated as a hypothesis to check, not a finding. |
| **[U]** | **Could not verify at all** — I have neither a snippet nor a reliable prior. Listed so it is not silently assumed. |

**Consequence for the gap analysis:** treat this document as a *source map and hypothesis
list*, not as a verified specification. Any design decision that depends on a precise
mechanic (a CT cost formula, a pod-activation radius, an initiative tie-break) must be
re-verified against the cited page with a working fetch/HTTP path before it is encoded
into engine requirements.

A recurring caveat: **many hits are user reviews, forums, or SEO mirrors.** Those are
marked inline as *weak source* and are used only for the existence of a mechanic, never
for its precise behaviour. The genuinely authoritative material found is: GDC Vault
pages, `gamedeveloper.com` (ex-Gamasutra) posts, official publisher/dev-blog rules and
manual PDFs, wiki concept pages, academic theses/papers, and one book on tabletop
mechanism vocabulary.

---

## 1. Taxonomy at a glance

**[I] for all classifications below; the linked sources are the evidence that the model
exists and is discussed, not that my classification is correct.**

| Model | Granularity of "whose turn" | Canonical exemplars | Anchor source found |
|---|---|---|---|
| IGOUGO / side-alternating | whole side | Advance Wars, Fire Emblem, Civ (SP) | [Nintendo Wars (pt.wikipedia)](https://pt.m.wikipedia.org/wiki/Nintendo_Wars), [Turn — Fire Emblem Wiki](https://fireemblem.fandom.com/wiki/Turn) |
| Initiative / CTB | one unit, from a computed queue | Final Fantasy Tactics, FFX, Fell Seal | [CT (Charge Time) Explained — Game8](https://game8.co/games/Final-Fantasy-Tactics/archives/543470), [Conditional Turn-Based Battle — Giant Bomb](https://giantbomb.com/wiki/Concepts/Conditional_Turn_Based_Battle) |
| ATB | one unit, from real-time-filling bars | FF IV–IX | [Final Fantasy's ATB battle system was inspired by race cars — RPS](https://www.rockpapershotgun.com/final-fantasy-ivs-active-time-battle-system-was-inspired-by-race-cars) |
| Phase-based | one phase, all (or many) units | Warhammer 40,000, Bolt Action | [The New Edition of Warhammer 40,000 Makes All the Phases Count — Warhammer Community](https://www.warhammer-community.com/en-gb/articles/W1JVMEow/the-new-edition-of-warhammer-40000-makes-all-the-phases-count/) |
| Group/weight-class initiative phases | a class of units | BattleTech (tabletop + HBS 2018) | [New BattleTech initiative system — BattleTech forums](https://battletech.com/forums/index.php?topic=73845.0) |
| Interleaved / alternating activation | one unit or one squad | XCOM 2 (side-level, unit-interleaved *within* the side), Into the Breach, Bolt Action | [xcom.fandom — Turn (XCOM 2)](https://xcom.fandom.com/wiki/Turn_(XCOM_2)), [Bolt Action Dev Diary 3: Order Die, Unit Activations and Snap To Action — Slitherine](https://www6.slitherine.com/news/bolt-action-dev-diary-3-order-die-unit-activations-and-snap-to-action) |
| Simultaneous resolution / WEGO | nobody; orders then resolve | Combat Mission, Frozen Synapse, BSG Deadlock | [WEGO Overlord Quick Guide (PDF, Matrix Games)](http://ftp.us.matrixgames.com/pub/WEGOWorldWarIIOverlord/WEGO%20Overlord%20Quick%20Guide%20printer-friendly.pdf), [Frozen Synapse — Giant Bomb](https://www.giantbomb.com/frozen-synapse/3030-26505/) |
| Turn-modifier / "press turn" | one unit, but turn *count* is spent by actions | Shin Megami Tensei, Metaphor: ReFantazio | [Press Turn System Guide — Game8 (SMT V)](https://game8.co/games/Shin-Megami-Tensei-V/archives/348265) |
| Real-time-with-pause hybrid | command points in a running clock | Valkyria Chronicles (BLiTZ) | [BLiTZ System (Concept) — Giant Bomb](https://giantbomb.com/wiki/Concepts/BLiTZ_System) |

---

## 2. Side-alternating / "I go, you go" (IGOUGO)

- **[S-]** Alternate whole-side turns are the model of the *Advance Wars* / *Nintendo
  Wars* family: the Wikipedia/archive article and the Giant Bomb game pages describe the
  series' turn-based structure — [Advance Wars (archived Wikipedia)](https://webarchiveweb.wayback.bac-lac.canada.ca/web/20051215000000/http://en.wikipedia.org/wiki/Advance_Wars),
  [Advance Wars: Dual Strike — Giant Bomb](https://giantbomb.com/wiki/Games/Advance_Wars_Dual_Strike),
  [Nintendo Wars (pt.wikipedia)](https://pt.m.wikipedia.org/wiki/Nintendo_Wars).
- **[S-]** *Wargroove* is the modern Advance-Wars-lineage exemplar; Chucklefish
  discussed its development publicly — [Watch Chucklefish's CEO and tech director
  discuss Wargroove's development — Game Developer](https://www.gamedeveloper.com/design/watch-chucklefish-s-ceo-and-tech-director-discuss-i-wargroove-s-i-development),
  [Wargroove Is A Very Good Strategy Game — Kotaku](https://kotaku.com/wargroove-is-a-very-good-strategy-game-1832196586).
- **[S-]** *Fire Emblem* uses explicitly named player/enemy turn phases: dedicated
  "Turn" pages exist on both the Fire Emblem Wiki and the Fandom wiki, and the diff
  history URL indicates the page is maintained — [Turn — Fire Emblem Wiki
  (fireemblemwiki.org)](https://fireemblemwiki.org/w/index.php?title=Turn&diff=269503&oldid=255190),
  [Turn — Fire Emblem Fandom](https://fireemblem.fandom.com/wiki/Turn).
- **[S-]** Civilization's *single-player* model is treated in the literature as an
  alternation, while its **multiplayer** offers a *different* scheduling mode
  ("Simultaneous Turns" vs "Dynamic Turns") — see §8.6. That a game ships **two
  scheduling models behind one ruleset** is itself an important engine finding.
- **[I]** The classic Civ model is described as "one human turn, then all AI turns
  processed", which is *not* symmetric IGOUGO — the human is one player among N and the
  AI moves are resolved in a batch that the player watches. This asymmetry has UI and
  interrupt consequences (see §10).
- **[I]** Structural consequences usually attributed to IGOUGO: (a) it is trivially
  serializable and deterministic; (b) the *first-mover* advantage is large, which is
  exactly what the game-design-pattern literature names as a known problem —
  [First Player Advantages (Game Design Patterns, virt10)](http://virt10.itu.chalmers.se/index.php?title=First_Player_Advantages&direction=prev&oldid=11112&printable=yes);
  (c) the non-active side has no agency during the opponent's turn, so "reaction" must
  be modelled as pre-committed state (overwatch) — see §10.2.
- **[S-]** The pattern itself is catalogued as **"Turn Taking"** in the Game Design
  Patterns collection — [Turn Taking (virt10)](http://virt10.itu.chalmers.se/index.php?title=Turn_Taking&direction=next&oldid=973&printable=yes),
  [Turn Taking (eliens mirror, VU Amsterdam)](http://few.vu.nl/~eliens/papers/media/@s5-pattern-turntaking.html).

---

## 3. Initiative-based / CTB (Charge Time Battle)

- **[S]** *Final Fantasy X* "uses a turn-based battle system called the Conditional
  Turn-Based Battle, or CTB for short" — stated verbatim in a search snippet:
  [Battle System — Final Fantasy X HD Remaster, Gamer Guides](https://www.gamerguides.com/final-fantasy-x-hd/guide/introduction/gameplay/battle-system).
- **[S-]** A dedicated concept page exists for CTB: [Conditional Turn-Based Battle
  (Concept) — Giant Bomb](https://giantbomb.com/wiki/Concepts/Conditional_Turn_Based_Battle).
- **[S-]** FFX has a per-action cost/priority stat distinct enough to warrant its own
  reference page, titled **"Rank"**: [Rank (Final Fantasy X) — Final Fantasy
  Wiki](https://finalfantasy.fandom.com/wiki/Rank_(Final_Fantasy_X)); the main system
  page is [Final Fantasy X battle system — Final Fantasy
  Wiki](https://finalfantasy.fandom.com/wiki/Final_Fantasy_X_battle_system).
- **[S]** *Final Fantasy Tactics* has a stat literally named **Charge Time (CT)** —
  stated in a result title: [CT (Charge Time) Explained — Game8, Final Fantasy
  Tactics](https://game8.co/games/Final-Fantasy-Tactics/archives/543470).
  Corroborating pages: [バトルシステムと戦闘のコツ (FFT battle system) —
  altema](https://altema.jp/fft/battlesystem).
- **[I]** The mechanism I believe both games share (NOT verified here): every combatant
  accumulates or is assigned a "time until next turn" value derived from a speed stat;
  the scheduler repeatedly picks the smallest, advances the clock to it, and the acting
  unit's *action* also sets its next arrival. The published CT list is therefore a
  **projection of a priority queue**, and acting early/late is a *choice* (waiting costs
  CT). **Formulas, tie-breaks, and whether the queue is recomputed on every action must
  be verified against the Game8/Fandom pages before being treated as spec.**
- **[S-]** The model is discussed as a genre-level design question, including its
  limited diffusion: [Why did CTB never take the JRPG world by storm? —
  Famiboards](https://famiboards.com/threads/why-did-ctb-never-take-the-jrpg-world-by-storm.18429/).
- **[S-]** A modern indie in the same family documents turn order publicly:
  [Turn order mechanics — Fell Seal: Arbiter's Mark (Steam
  discussion)](https://steamcommunity.com/app/699170/discussions/0/2575445991836867685),
  with a speed-theorycraft thread asking "given a SPD value, how long does it take for a
  unit to be able to take two turns before the enemy can act?" —
  [Fell Seal Steam discussion](https://steamcommunity.com/app/699170/discussions/0/3425571923627145601/).
  *These are community threads (weak-to-moderate sources), but the second one is direct
  evidence that players reason about the **scheduler's arithmetic**, i.e. that the
  scheduler is a legible, tunable system rather than a hidden one.*
- **[S-]** An academic treatment of the family exists — [Parametric Analysis of
  Turn-Based RPG Combat Systems: Design for Scalable Parties — IEEE
  Xplore](https://ieeexplore.ieee.org/document/11420395/keywords) — title only;
  **[U]** its actual parameters.
- **[S]** A thesis on the *consequences* of ordering carries an explicit thesis claim in
  the snippet: "The order in which units take their turn can dramatically affect the
  outcome of the game" — [Bachelor thesis, CTU Prague (dspace.cvut.cz,
  PDF)](https://dspace.cvut.cz:443/bitstream/handle/10467/107001/F3-BP-2022-Boburka-Viktor-Bakalarska%20prace%20Viktor%20Boburka.pdf?sequence=-1&isAllowed=y).
  This is the strongest available statement that **turn order is a first-class balance
  variable, not an implementation detail.**
- **[S]** A worked implementation of initiative scheduling with a priority queue is
  documented academically: [Turn-Based Batch Scheduling in "Reincarnation Journey:
  Fantasy Fate" using Priority Queues (ITB, PDF)](https://informatika.stei.itb.ac.id/~rinaldi.munir/Matdis/2024-2025-2/Makalah2025/Makalah-Matdis-2025-IF-ITB%20(22).pdf).
- **[S-]** Adjacent initiative-driven systems worth mining: *Tactics Ogre*
  ([CBR feature](https://www.cbr.com/tactics-ogre-yasumi-matsuno-underrated-masterpiece/)),
  *Divinity: Original Sin 2* — a guide snippet states "The game will determine the turn
  order by evaluating everyone's initiative" ([Steam guide, it-IT](https://steamcommunity.com/sharedfiles/filedetails/?l=italian&id=1147787126)),
  *Baldur's Gate 3* ([Combat Guide: Basics of Combat Explained —
  Game8](https://game8.co/games/BG3/archives/419902),
  [How To Improve Initiative In Combat — GameSpot](https://www.gamespot.com/articles/baldurs-gate-3-how-to-improve-initiative-in-combat/1100-6516602/)),
  and *Wakfu*'s initiative system, including a player-coined pathology term
  (["Initiative Rape?" — Wakfu forums](https://www.wakfu.com/en/forum/569-xelor/60379-initiative-rape),
  [How Did Initiative Change? — Wakfu forums](https://www.wakfu.com/en/forum/569-xelor/174747-how-initiative-change)).
- **[S-]** *Marvel's Midnight Suns* runs a card-driven, initiative-ordered turn model —
  [Marvel's Midnight Suns: card tactics and turn-based superhero combat explained —
  PlayStation.Blog](https://blog.fr.playstation.com/2022/10/26/marvels-midnight-suns-tactiques-de-cartes-et-combats-de-superheros-au-tour-par-tour-expliques/).
- **[S-]** *Shin Megami Tensei* / *Metaphor: ReFantazio* add a **turn-economy** layer on
  top of ordering ("Press Turn" — spending/refunding turn icons) —
  [Press Turn System Guide — Game8 (SMT V)](https://game8.co/games/Shin-Megami-Tensei-V/archives/348265),
  [Press Turn Battle System Guide — Game8 (SMT III Nocturne)](https://game8.co/games/Shin-Megami-Tensei-III-Nocturne/archives/332175),
  [Battle System Explained — Game8 (Metaphor: ReFantazio)](https://game8.co/games/Metaphor-ReFantazio/archives/475379),
  [Atlus at GDC: Developing Metaphor's Battle System and UI — ResetEra](https://www.resetera.com/threads/atlus-at-gdc-developing-metaphors-battle-system-and-ui.1156995/),
  [Atlus discusses the potential of turn-based battle systems… — RPG
  Site](https://www.rpgsite.net/feature/17065-atlus-discusses-potential-turn-based-battle-systems-through-lens-metaphor-refantazio).
  *Design point for the engine: **order** and **budget** are separable concerns; a
  scheduler that only yields "whose turn" cannot express Press Turn.*
- **[I]** Common CTB/initiative failure modes designers report (unverified here):
  initiative "dump stats" that make speed dominant, ties resolved invisibly, and a
  legibility problem where the player cannot predict the queue after a speed buff.

---

## 4. ATB (Active Time Battle) — and how it differs from CTB

- **[S]** The design origin is documented in a press headline: "Final Fantasy's ATB
  battle system was originally inspired by race cars", with the URL slug naming **FFIV**
  — [Rock Paper Shotgun](https://www.rockpapershotgun.com/final-fantasy-ivs-active-time-battle-system-was-inspired-by-race-cars).
- **[S-]** The credited designer has a Wikipedia page — [Hiroyuki Itō —
  Wikipedia](https://en.wikipedia.org/wiki/Hiroyuki_It%C3%B4). *The page title alone does
  not establish the ATB attribution; **[I]** that Itō designed ATB is widely repeated
  but was not verified here.*
- **[S]** A snippet describing ATB in Spanish states the system "was played in
  semi-real time, and assigned each creature in combat a **time bar**" (my translation of
  "El sistema de ATB era jugado en tiempo semi-real, y asignaba a cada criatura en el
  combate una barra de tiempo") — [Final Fantasy (diff) — ecured.cu](https://www.ecured.cu/index.php?title=Final_Fantasy&diff=581975&oldid=581926).
  *This is a wiki mirror (moderate source) but it is the only snippet found that states
  the ATB mechanism rather than only naming it.*
- **[S-]** Supporting encyclopedia entries: [ATB（游戏战斗模式）— 百度百科](https://wapbaike.baidu.com/item/ATB/20629654),
  [Final Fantasy — icd-11.org mirror](http://icd-11.org/index.php?title=Final_Fantasy) *(weak source)*.
- **[S-]** FFVII Remake's "Classic Mode" is discussed as an ATB-mode question in
  mainstream press — [Final Fantasy 7 Remake's Classic Mode Won't Satisfy Purists —
  IGN](https://me.ign.com/en/ps4/165527/final-fantasy-7-remakes-classic-mode-wont-satisfy-purists).
- **[S-]** FFXIII's battle system (a further variant) has press documentation:
  [Final Fantasy XIII Battle System Detailed — IGN](https://www.ign.com/articles/2007/03/02/final-fantasy-xiii-battle-system-detailed-3).
- **[I] The ATB-vs-CTB distinction as I understand it (NOT verified here):**
  - *ATB* is a **real-time accumulator**: each combatant's gauge fills continuously
    against wall-clock time; combat is "semi-real-time". Two sub-modes are classically
    offered — **Active** (gauges keep filling while the player browses menus) and
    **Wait** (gauges freeze while menus are open). That mode switch is exactly what the
    ecured snippet gestures at with "semi-real time".
  - *CTB* removes wall-clock time entirely: it is a **discrete, event-driven queue**.
    Turn order is computable in advance (FFX displays the projected order) and can be
    *recomputed* as a consequence of actions (haste/slow/delay).
  - **Engine implication:** ATB needs a *tick/clock driver* plus a *pause policy*;
    CTB needs *no clock at all* but needs a *fully re-derivable priority queue* and (for
    UI) a *queue projection* function. These are genuinely different runtime services;
    see §11.
  - **[U]** The precise FFIV–VI Active/Wait default settings per title, and per-title
    gauge-speed constants, were not verified.
- **[S-]** A roguelike/RPG-engine doc explicitly separates these families in its
  implementation guidance — [Battle System Differences — VisuStella MZ wiki
  (yanfly.moe)](https://yanfly.moe/wiki/index.php?diff=next&oldid=7893&title=Battle_System_Differences_VisuStella_MZ),
  and a general comparison thread: [Summary of Battle Systems? — RPG Maker
  Web](https://forums.rpgmakerweb.com/threads/summary-of-battle-systems.45783/).

---

## 5. Phase-based turns (movement / shooting / assault) and unit-by-unit interleaving

- **[S]** The 40k phase model is the canonical case, and Games Workshop itself framed an
  edition around it: "The New Edition of Warhammer 40,000 Makes All the Phases Count" —
  [Warhammer Community](https://www.warhammer-community.com/en-gb/articles/W1JVMEow/the-new-edition-of-warhammer-40000-makes-all-the-phases-count/).
- **[S-]** Official rules documents exist and are reachable as PDFs —
  [Warhammer 40,000 rules PDF (warhammer40000.com)](https://warhammer40000.com/wp-content/uploads/2023/06/x8HeGjzdBWm5haRZ.pdf),
  [Warhammer Community rules PDF](https://www.warhammer-community.com/wp-content/uploads/2020/07/n7V0DjD9ja1DoQ1A.pdf).
  **[U]** — their contents were not retrieved, so the exact phase list/order per edition
  is unverified.
- **[S]** A community data project exposes phases as a **machine-readable type** in its
  generated API, which is direct evidence that phase order is being modelled as data in
  third-party tooling: [`Phase` type alias — 40kdc-data generated
  docs](https://raw.githubusercontent.com/wn-mitch/40kdc-data/4e80d96efec2cbd1cc91c42ca3fccf1661a23656/tools/docs/api/generated/type-aliases/Phase.md).
- **[S]** **Alternating activation is a live design argument in the tabletop world**, not
  just an implementation choice: [Alternative Activation — is it desired by the community
  or not? — DakkaDakka](https://dakkagames.com/dakkaforum/posts/blog/815790.page;jsessionid=ADBB8AA76D47F696735E9E04F3AF02FC),
  [Goonhammer Historicals: Activation Roundtable —
  Goonhammer](https://www.goonhammer.com/goonhammer-historicals-activation-roundtable/).
  The *Roundtable* format implies multiple competing activation schemes are in use.
- **[S]** **Bolt Action** implements "order dice + unit activation + snap-to-action" —
  stated in a developer diary title: [Bolt Action Dev Diary 3: Order Die, Unit
  Activations and Snap To Action — Slitherine](https://www6.slitherine.com/news/bolt-action-dev-diary-3-order-die-unit-activations-and-snap-to-action).
  A press piece separately reports six order types — [Bolt Action PC Reveals Gameplay:
  Six Orders and Suppression Come to Life — Tech Times](https://www.techtimes.com/articles/321381/20260723/bolt-action-pc-reveals-gameplay-six-orders-suppression-come-life.htm)
  (weak-moderate source), plus [Bolt Action : aperçu du gameplay —
  Wargamer.fr](https://www.wargamer.fr/bolt-action-apercu-du-gameplay/).
  **[I]** In Bolt Action the draw of an order die names *which* unit activates next, so
  activation order is randomized-but-legible and every unit acts exactly once per round;
  I could not verify this from retrieved content.
- **[S-]** A formal-game-proposal document uses the phrase "Turn-by-turn battle flow for
  player and enemy" — [GPR Formal Game Proposal (PDF, collab.dvb.bayern)](https://collab.dvb.bayern/download/attachments/2114405310/GPR___Formal_Game_Proposal.pdf).
  *(Low-authority source; cited only as evidence that the phrase is used in design
  documentation.)*
- **[I]** Phase-based design's structural property: **the phase list is a state machine
  over subsets of units**, so the same unit can be visited several times per round in
  different phases, and "has this unit moved yet?" must be tracked as *per-phase* rather
  than per-round state. This is the key modelling difference from IGOUGO/CTB, where a
  unit's turn is a single contiguous token.
- **[S-]** The general vocabulary for this family of mechanism ("stat turn order",
  activation, etc.) is catalogued in a tabletop mechanism encyclopedia that surfaced in
  search with the identifier **"TRN-02 STAT TURN ORDER"** — [An Encyclopedia of
  Mechanisms: Building Blocks of Tabletop Game Design (full text, Internet
  Archive)](https://archive.org/stream/0_20240430_202404/An%20Encyclopedia%20of%20Mechanisms%EF%BC%9ABuilding%20Blocks%20of%20Tabletop%20Game%20Design_djvu.txt).
  *The snippet shows a numbered taxonomy entry; the URL is a full-text dump, so it is a
  usable primary-ish reference for mechanism vocabulary if fetched later.*

---

## 6. Interleaved / alternating-activation turns

### 6.1 XCOM 2's structure and pod activation

- **[S-]** XCOM 2 has enough scheduling structure to warrant a dedicated wiki concept
  page — [Turn (XCOM 2) — XCOM Wiki](https://xcom.fandom.com/wiki/Turn_(XCOM_2)).
- **[S]** XCOM 2's per-unit budget is a small integer that players find ambiguous:
  "Never Clear on what costs 1 or 2 action points" — [Steam
  discussion](https://steamcommunity.com/app/268500/discussions/0/366298942110338752).
  *Weak source, but it evidences that the turn is composed of **action points**, not a
  single move-or-act decision.*
- **[S]** **Pods are a first-class grouping construct in the AI, not just flavour**: an
  enemy-group abstraction is visible both in a wiki force listing
  ([XCOM 2 / Alien Forces — StrategyWiki](https://strategywiki.org/wiki/XCOM_2/Alien_Forces))
  and in persistent modding bug reports about activation edges — a Long War 2 forum
  thread titled "Non-activated pods don't activate when seeing hacked bots, but still
  shoot at them" — [Pavonis Interactive forums](https://www.pavonisinteractive.com/phpBB3/viewtopic.php?p=28639),
  and a mod discussion literally about making "pods not suck" — [Steam
  discussion](https://steamcommunity.com/app/268500/discussions/0/598523169276476233).
  A community strategy point about overwatch/activation interaction appears in a forum
  archive — [XCOM 2 — War of the Chosen (giantitp archive)](https://forums.giantitp.com/archive/index.php/t-527146.html).
- **[S-]** XCOM 2 also has a pre-combat **concealment phase** documented as its own
  guide chapter — [XCOM 2: Faza ukrycia (Concealment) —
  GRYOnline.pl](https://www.gry-online.pl/poradniki/xcom-2/faza-ukrycia-concealment/z314329),
  and an onboarding article — [XCOM 2 Hints and Tips: How to run a resistance —
  Prima Games](https://primagames.com/gaming/xcom-2-hints-and-tips-how-to-run-a-resistance).
- **[S-]** XCOM 2's AI was publicly discussed by its developers alongside other titles:
  [Video: Improving AI in Assassin's Creed III, XCOM, Warframe — Game
  Developer](https://www.gamedeveloper.com/design/video-improving-ai-in-i-assassin-s-creed-iii-xcom-warframe-i-).
- **[I]** The pod model as generally understood (NOT verified here): enemies are
  grouped; a group activates as a unit when any member gains line-of-sight to the
  player's squad (usually on the player's turn), and the *entire* pod then receives a
  free "scamper" move before normal alternating play resumes. This creates the
  characteristic **activation cascade**: pulling one pod can pull others if sightlines
  overlap, which players call a "pod chain". The consequence for the engine is that
  **the scheduler must support out-of-band insertion of a whole group's actions in the
  middle of the player's phase** — i.e. the turn order is not a fixed list.
- **[I]** Within a side, XCOM alternates freely among your own units ("unit-by-unit
  interleaved within a side"), and each unit has a bounded action budget. "End turn"
  hands the whole side over. **[U]** exact order in which AI units act within their turn.

### 6.2 Into the Breach — player phase, then fully telegraphed enemy phase

- **[S]** The canonical talk exists on GDC Vault: [Into the Breach Design Postmortem —
  GDC Vault](https://gdcvault.com/play/1026333/-Into-the-Breach-Design).
- **[S]** A companion talk coverage piece: [Video: How Subset Games designed Into the
  Breach — Game Developer](https://www.gamedeveloper.com/design/video-how-subset-games-designed-i-into-the-breach-i-).
- **[S]** The developers published an AI writeup whose visible snippet frames the design
  problem around difficulty tuning: "It's hard for me not to write way too much on this
  subject, but AI / balancing / difficulty was the biggest challenge in making …" —
  [Into the Breach AI (PDF, Internet Archive)](http://archive.org/download/into-the-breach-ai/Into%20the%20Breach%20AI.pdf).
  **This is the closest thing found to a primary developer statement about enemy-phase
  design.**
- **[S-]** The design is described in press as a fusion of puzzle and tactics —
  [Into the Breach: FTL follow-up details — RPS](https://www.rockpapershotgun.com/into-the-breach-details-preview),
  [FTL follow-up Into the Breach is great — RPS](https://www.rockpapershotgun.com/into-the-breach-preview-tactics),
  [Into the Breach (Wikipedia PDF)](http://en.wikipedia.org/api/rest_v1/page/pdf/Into_the_Breach),
  [Into the Breach — ja.wikipedia](https://ja.wikipedia.org/wiki/Into_the_Breach).
- **[S-]** Community discussion of enemy action ordering exists on the developer's own
  forum — [NPC Action — Subset Games forums](https://www.subsetgames.com/forum/viewtopic.php?t=33412).
- **[I]** The defining property (NOT verified here): the player's phase is fully
  **perfect-information**, and the enemy phase is **deterministic and pre-announced** —
  every enemy intent (move target, attack target, attack order) is displayed before the
  player commits. Enemy attacks resolve in a fixed, legible order. The consequence is
  that the game is a *deterministic puzzle with a known resolution function*, which is
  precisely what makes "undo turn" and reset-the-timeline mechanics coherent. For an
  engine this requires **intent objects produced by the AI during the player phase**,
  stored on the scheduler, and replayed in a fixed order during the enemy phase.
- **[U]** Whether attack order among enemies is by spawn/roster index, by an internal
  priority, or by tile order — unverified.

### 6.3 Bolt Action / tabletop alternating activation

Covered in §5; the Slitherine dev diary is the strongest source found
([link](https://www6.slitherine.com/news/bolt-action-dev-diary-3-order-die-unit-activations-and-snap-to-action)).

---

## 7. Simultaneous resolution / WEGO

- **[S]** "WEGO" is an established, *named* term in the wargame market — Matrix Games
  ships a product literally titled **WEGO World War II: Overlord**, whose quick guide is
  online: [WEGO Overlord Quick Guide (PDF)](http://ftp.us.matrixgames.com/pub/WEGOWorldWarIIOverlord/WEGO%20Overlord%20Quick%20Guide%20printer-friendly.pdf).
- **[S-]** The terminology is used by players/publishers in forum threads: [More Wego —
  Matrix Games forums](https://www1.matrixgames.com/forums/printable.asp?m=841795),
  [Very late to the party — "is the PBEM WEGO?" — Matrix Games
  forums](https://www1.matrixgames.com/forums/printable.asp?m=4000255).
- **[S-]** **Combat Mission** is the reference implementation of WEGO, and its manual is
  available — [Combat Mission Red Thunder Manual (PDF, Matrix
  Games)](http://ftp.eu.matrixgames.com/pub/CombatMissionRedThunder/CMRedThunderManual3.01.pdf).
  *This manual is the single best candidate primary source for turn-*resolution* order,
  but **[U]** its contents were not retrieved.*
- **[S-]** A retrieved snippet describes exactly the order-writing/execution split:
  "In turn 3 and 4 the first player issued commands are executed" —
  [core.ac.uk PDF](https://core.ac.uk/download/612071353.pdf). *Title/snippet only; the
  surrounding document is unidentified.*
- **[S-]** **Frozen Synapse** is the real-time-flavoured WEGO exemplar: a GDC 2012
  Famitsu article frames it as taking "the best of both real-time and turn-based"
  (「リアルタイムとターン制のいいとこ取り」) — [ファミ通.com](https://www.famitsu.com/news/201203/14011482.html).
  Publisher/retail and review evidence: [Frozen Synapse — Slitherine](https://www6.slitherine.com/game/frozen-synapse),
  [Matrix Games to Publish Frozen Synapse!](http://www1.matrixgames.com/forums/tm.asp?m=2820626),
  [Frozen Synapse — Giant Bomb](https://www.giantbomb.com/frozen-synapse/3030-26505/),
  [Frozen Synapse Review — GameSpot](https://www.gamespot.com/reviews/frozen-synapse-review/1900-6327044/),
  [Frozen Synapse Prime Review — IGN](https://in.ign.com/frozen-synapse-tactics/67601/frozen-synapse-prime-review).
- **[S-]** **Battlestar Galactica Deadlock** is the mainstream-visible WEGO title —
  [Battlestar Galactica Deadlock — Wikipedia](https://en.m.wikipedia.org/wiki/Battlestar_Galactica_Deadlock),
  [Battlestar Galactica: Deadlock Review — IGN](https://www.ign.com/articles/2017/12/13/battlestar-galactica-deadlock-review-2),
  [IGN SEA mirror](https://sea.ign.com/battlestar-galactica-deadlock/126987/battlestar-galactica-deadlock-review).
  **[I]** Deadlock's loop is generally described as: plan each ship's manoeuvre and
  firing orders in a pause/planning phase, then watch a short simultaneous "execution"
  reel; weapons fire opportunistically as targets come into arc. **Unverified here.**
- **[S-]** RPS's design column discusses WEGO-adjacent scheduling in a strategy context:
  [The Flare Path: Field of Glory: Empires — RPS](https://www.rockpapershotgun.com/field-of-glory-empires),
  [The Flare Path talks Task Force Admiral — RPS](https://www.rockpapershotgun.com/the-flare-path-talks-task-force-admiral).
- **[I]** The core engine property of WEGO (NOT verified here) is a **two-phase
  scheduler with a resolution projector**: (1) a *write* phase in which each side's
  orders are recorded against units without world mutation, and (2) a *resolve* phase
  that advances a simulation — usually in small ticks — during which pre-committed
  orders may be invalidated (target died, path blocked) and must degrade gracefully.
  This makes WEGO the model with the **largest "invalidated order" surface**, and is why
  WEGO implementations need an explicit order-revalidation/opportunity-fire policy.
- **[I]** WEGO is also the model most naturally aligned with **simultaneous turn
  resolution for multiplayer** (order submission, then a shared resolution), which
  connects to PBEM conventions referenced in the Matrix threads above.

---

## 8. Per-game notes

### 8.1 XCOM (1994) / UFO: Enemy Unknown
- **[S-]** The original's **Time Unit** system is documented in the open-source remake
  UFO:AI's manual — [Manual/Singleplayer/Time Units v2.5 — UFO:AI wiki](https://ufoai.org/w/index.php?title=Manual/Singleplayer/Time_Units/v2.5&feed=atom&action=history).
  *Caveat: this is UFO:AI's manual, not the 1994 MicroProse manual, though UFO:AI is an
  explicit reimplementation of that ruleset — the forum thread [Essence of
  XCOM-UFO — UFO:AI forums](https://ufoai.org/forum/index.php/topic,3741.msg28048.html)
  shows the community treating the lineage as continuous.*
- **[S]** The original's **reaction fire** is a real, bug-prone subsystem evidenced by a
  dedicated bug thread — [Reaction fire bugs (was: reaction fire triggers though should
  not) — UFO:AI forums](https://ufoai.org/forum/index.php/topic,8667.msg62529.html).
- **[I]** The 1994 model differs from XCOM 2 in a way that matters architecturally: the
  player's squad acts **unit-by-unit**, each unit spending from an individual TU pool
  (snap/aimed/auto shots cost differing TU), and the turn does not end until the player
  ends it or TUs are exhausted; the AI side then plays. TU also *funds reaction fire*,
  so a unit that spent all its TUs cannot react. This makes **reaction fire a
  consequence of a shared resource**, not a separate ability — a significant design
  difference from XCOM 2's discrete Overwatch ability. **[U]** verified numbers.

### 8.2 XCOM 2
See §6.1. **[S]** distinct wiki term page, **[S]** action-point budget, **[S]** pods,
**[S]** concealment phase.

### 8.3 Fire Emblem
- **[S-]** Player phase / enemy phase is the model; the mechanic has its own maintained
  wiki page — [Turn — Fire Emblem Wiki](https://fireemblemwiki.org/w/index.php?title=Turn&diff=269503&oldid=255190),
  [Turn — Fire Emblem Fandom](https://fireemblem.fandom.com/wiki/Turn).
- **[S-]** The series' turn structure carries into its mobile spinoff — [Fire Emblem
  Heroes — Wikipedia](https://en.m.wikipedia.org/wiki/Fire_Emblem_Heroes),
  [Fire Emblem: Awakening — pt.wikipedia](https://pt.wikipedia.org/wiki/Fire_Emblem_Awakening).
- **[S-]** An open-source AI-coding project documents a Fire Emblem turn system as a
  design doc, i.e. an independent restatement of the model —
  [fire_emblem/docs/09_turn_system.md — GitHub](https://github.com/yeshan333/x-gamedev-with-ai/blob/main/fire_emblem/docs/09_turn_system.md).
  *(Low authority, but useful as a machine-readable restatement.)*
- **[I]** Within a player phase the player activates units in **any order**, each unit
  generally acting once; there is no initiative stat. Counterattacks are positional
  (adjacent, weapon-triangle-dependent) rather than scheduled. **[U]** exact
  counterattack resolution ordering.

### 8.4 Final Fantasy Tactics (CT)
See §3. **[S]** CT = Charge Time (Game8 title, altema). **[I]** CT is also used as an
in-fiction *cast timer* (spells land after N CT), which means one stat serves both the
scheduler and per-action delay — a dual-role worth noting for engine design.
**[S-]** A Spanish mirror article exists for FFT's battle system —
[Final Fantasy Tactics (diff) — ecured.cu](https://www.ecured.cu/index.php?title=Final_Fantasy_Tactics&diff=prev&oldid=1217684).

### 8.5 Into the Breach
See §6.2.

### 8.6 BattleTech (2018, HBS) — initiative phases
- **[S-]** BattleTech has a formally named **initiative system**, and its alternatives
  are argued about by the player base — [New BattleTech initiative system — BattleTech
  forums](https://battletech.com/forums/index.php?topic=73845.0),
  [same thread, reply view](https://bg.battletech.com/forums/index.php?PHPSESSID=6u8at5nvqilgseqd8oj6fpu0om&topic=73845.msg1730251#msg1730251),
  [Any officially created Initiative alternatives? — BattleTech
  forums](https://battletech.com/forums/index.php/topic,54827.msg1267177.html),
  [bg.battletech.com forum view](https://bg.battletech.com/forums/index.php?PHPSESSID=6u8at5nvqilgseqd8oj6fpu0om&action=printpage;topic=73845.0).
  *The existence of a long-running "official alternatives" thread is strong evidence
  that initiative here is a **tunable rules module**, not a fixed engine constant.*
- **[S-]** Official quick-start rules are published — [BattleTech Quick Start Rules —
  battletech.com](https://battletech.com/qsr/),
  [Salvage Box Quick Start Rules (PDF)](https://bg.battletech.com/wp-content/uploads/1645/44/Salvage-Box-Quick-Start-Rules.pdf).
  **[U]** their content.
- **[I]** The commonly described model (NOT verified here): units are sorted into
  initiative brackets by weight class (Light → Medium → Heavy → Assault); within a
  bracket the two sides alternate playing one unit each, with the *loser* of the
  initiative roll choosing who moves first in the first bracket (and thus often
  "activating last" in the round). This is a **hybrid: sorted phases + side alternation
  + a bidding mechanic**. **[U]** exact bracket order and whether the 2018 video game
  matches the tabletop exactly.

### 8.7 Civilization
- **[S]** Civ multiplayer exposes scheduling modes by name — "Simultaneous Turns" vs
  "Dynamic Turns" — [How to change a game from Simultaneous Turns to Dynamic Turns and
  hotseat/multiplayer/singleplayer? — Arqade](https://gaming.stackexchange.com/feeds/question/347352);
  a Russian-language Civ VI multiplayer thread is titled "Simultaneous turns for all
  players" — [Steam discussion](https://steamcommunity.com/app/289070/discussions/4/135507548127169842/).
- **[S-]** Civ V multiplayer scheduling was covered in the press before release —
  [Civilization 5 Multiplayer Preview — Shacknews](https://www.shacknews.com/article/65521/civilization-5-multiplayer-preview).
- **[S-]** A GDC-side talk on Civ VII's narrative integration exists (Chinese-language
  coverage) — [GDC25 | 文明研发团队演讲 — GameLook](http://www.gamelook.com.cn/2025/03/566892/);
  unrelated to scheduling, listed only as a Civ GDC pointer.
- **[S-]** A design retrospective on *Civilization: Beyond Earth* exists in video form —
  [Video: A design retrospective of Civilization: Beyond Earth](https://www.jikguard.com/news/video-a-design-retrospective-of-civilization-beyond-earth/)
  *(third-party repost; the original is a GDC talk — **[U]** exact GDC Vault URL).*
- **[I]** Single-player Civ is effectively **sequential-with-interleaving**: the human
  issues all orders for one turn, then the AI civilizations each take a full turn in a
  fixed order, then the human's orders are resolved during a "next turn" processing
  step. Movement is executed with per-unit path/ordering rules. This means Civ does
  **not** cleanly fit IGOUGO — it is closer to **order-then-resolve with a processing
  barrier**, which is why simultaneous multiplayer is a small step rather than a
  rewrite. **[U]** verified.

### 8.8 Advance Wars
- **[S-]** Side-alternating day/turn structure with per-side "turn" concept —
  [Advance Wars (archived Wikipedia)](https://webarchiveweb.wayback.bac-lac.canada.ca/web/20051215000000/http://en.wikipedia.org/wiki/Advance_Wars),
  [Advance Wars: Dual Strike — Giant Bomb](https://giantbomb.com/wiki/Games/Advance_Wars_Dual_Strike),
  [Hands-on Advance Wars — GameSpot](https://www.gamespot.com/app.php/articles/hands-on-advance-wars/1100-2780790/),
  [Nintendo Wars — pt.wikipedia](https://pt.m.wikipedia.org/wiki/Nintendo_Wars).
- **[S-]** A modern review of the remake describes the structure for a mainstream
  audience — [Advance Wars 1+2: Re-Boot Camp review — Stuff.tv](https://www.stuff.tv/review/advance-wars-1-2-re-boot-camp-review/).
- **[I]** Within a side's turn, units activate in any order and "end turn" is explicit.
  Day counter increments per full round; a commanding-officer power is charged by the
  side's own actions and consumed at the start of its own next turn — i.e. **there is a
  second, hidden scheduling axis (CO meter) alongside the turn counter**. **[U]**
  verified numbers.

---

## 9. How designers choose a scheduling model

- **[S]** The Game Design Patterns catalogue treats turn order as a *pattern choice*
  with named consequences, including first-player advantage and analysis paralysis —
  [Turn Taking](http://virt10.itu.chalmers.se/index.php?title=Turn_Taking&direction=next&oldid=973&printable=yes),
  [First Player Advantages](http://virt10.itu.chalmers.se/index.php?title=First_Player_Advantages&direction=prev&oldid=11112&printable=yes),
  [Analysis Paralysis](http://virt10.itu.chalmers.se/index.php?title=Analysis_Paralysis&direction=next&oldid=10213&printable=yes),
  [designing games to prevent analysis paralysis, part 2 — League of
  Gamemakers](https://www.leagueofgamemakers.com/designing-games-to-prevent-analysis-paralysis-part-2/).
- **[S]** Keith Burgun's *Clockwork Game Design* devotes chapters to the theory and to
  the *pitfalls* of turn structure — [Theory (ch. 2), Clockwork Game
  Design](https://www.taylorfrancis.com/chapters/mono/10.4324/9781315756516-2/theory-keith-burgun?context=ubx&refId=d6b81122-a4c3-4b0f-96ca-4bd492968e4c),
  [Pitfalls (ch. 4, 2nd ed.), Clockwork Game
  Design](https://www.taylorfrancis.com/chapters/mono/10.1201/9781003484547-4/pitfalls-keith-burgun?context=ubx&refId=6bb8650d-ad09-4790-a249-3b745a22257c);
  his blog archive is [tactics — keithburgun.net](http://keithburgun.net/tag/tactics/page/2/).
  **[U]** the actual arguments — paywalled/not retrieved.
- **[S]** Sirlin's *Codex* design diaries discuss turn-order/first-player asymmetry as a
  balance problem, and the community treats P1/P2 roles as distinct strategies —
  [Codex design diary: a rocky road — Sirlin.Net](http://oldsite.sirlin.net/blog/2014/3/25/codex-design-diary-a-rocky-road.html?printerFriendly=true),
  [The Game Design of Codex — Sirlin Games forums](https://forums.sirlingames.com/t/the-game-design-of-codex/332/4),
  [Strategy: Codex Roles P1 vs. P2 — Sirlin Games forums](https://forums.sirlingames.com/t/strategy-codex-roles-p1-vs-p2/1905/18).
- **[S-]** BrainGoodGames discusses turn-structured design decision-making in its design
  commandment series — [BrainGoodGames Design Commandment #4: Moving Forwards — Game
  Developer](https://www.gamedeveloper.com/design/braingoodgames-design-commandment-4-moving-forwards),
  [turtling — BrainGoodBlog](https://blog.braingoodgames.com/tag/turtling/).
- **[S-]** *Halfway*'s mechanics were written up on Game Developer, as a case study in
  turn-structured tactics design — [The Mechanics of Halfway — Game
  Developer](https://www.gamedeveloper.com/design/the-mechanics-of-halfway).
- **[S]** A designer blog piece specifically about designing a clockwork/scheduled combat
  system — [Designing Silicon Void's Combat, Part 1: Clockwork — Game
  Developer](https://www.gamedeveloper.com/design/designing-silicon-void-s-combat-part-1-clockwork).
- **[S]** A Battle Brothers developer blog series covers its tactical combat and turn
  ordering — [Dev Blog #4 Tactical combat mechanics in Battle Brothers —
  battlebrothersgame.com](http://battlebrothersgame.com/tactical-combat-mechanics/),
  [Tactical Combat — Battle Brothers Developer Blog](http://battlebrothersgame.com/tactical-combat/).
  A bug-report thread shows an ordering-affecting status effect in the shipped game
  ("Necrosavants still act first when staggered") —
  [Battle Brothers forums](http://battlebrothersgame.com/forums/topic/necrosavants-still-act-first-when-staggered/).
  *This is a good concrete example: a status effect must be able to **reorder** the
  schedule mid-round, which a naive "sorted list computed once per round" cannot do.*
- **[S]** A podcast episode dedicated to the topic of **initiative** exists — [Initiative
  — The Misdirected Mark Podcast (via Muck Rack)](https://muckrack.com/podcast/the-misdirected-mark-podcast/episodes/1487687-initiative-the-misdirected-mark-podcast-ta/).
- **[S-]** *Dust Warfare*'s designer diary discusses its activation scheme — [Dust
  Warfare Designer Diary, Part Two — Fantasy Flight
  Games](https://drafts.fantasyflightgames.com/en/news/2011/11/4/dust-warfare-designer-diary-part-two/).
- **[I]** The emergent decision rubric I would expect (and which the sources above are
  consistent with, but do **not** state): choose the model from the *fantasy* you want —
  IGOUGO for a clean "my army's moment" and maximum AI simplicity; initiative/CTB for
  build-craft around speed and for skill-expression via delay/reorder; ATB for tension
  and reaction pressure; phases for a shared, teachable rules vocabulary and rich
  interaction windows; WEGO for command friction, replay drama, and PBEM-able
  multiplayer. **[U]** — no source retrieved states this rubric as such.

---

## 10. Consequences for AI, UI, and multiplayer determinism

### 10.1 AI
- **[S]** Developer talks on turn-based/tactical AI exist and are indexed — [Video:
  Improving AI in Assassin's Creed III, XCOM, Warframe — Game
  Developer](https://www.gamedeveloper.com/design/video-improving-ai-in-i-assassin-s-creed-iii-xcom-warframe-i-).
- **[S]** Into the Breach's AI/difficulty writeup (PDF) is the most directly relevant
  primary artifact found — [Into the Breach AI (PDF)](http://archive.org/download/into-the-breach-ai/Into%20the%20Breach%20AI.pdf).
- **[S-]** An OSS project keeps an AI architecture spec for a turn-based strategy game,
  including turn/phase interactions — [colonizethisv3 SPEC/ai/ai-architecture.md —
  GitHub](https://github.com/waigore/colonizethisv3/blob/main/SPEC/ai/ai-architecture.md).
- **[I]** Architectural claim (NOT verified here): the scheduling model determines the
  AI's *planning horizon shape*. IGOUGO lets the AI plan one contiguous side-turn;
  interleaved/CTB forces incremental or reactive planning because the world changes
  between its units' actions; WEGO forces **plan-under-uncertainty with a
  devalidation pass**; ITB-style telegraphed phases let the AI be a *deterministic,
  fully-searchable* component (and makes the enemy an authorable, testable function).
  A TBS engine therefore cannot treat "AI" and "scheduler" as independent modules: the
  AI needs a contract with the scheduler (who acts next, when may it think, what may it
  assume).

### 10.2 Reaction / interrupt / overwatch as a scheduling feature
- **[S]** Reaction fire is a named design topic in a designer blog — [Game Design #39:
  Reaction Moves, Reaction Fire — Delta Vector](http://deltavector.blogspot.com/2015/04/game-design-39-reaction-moves-reaction.html).
- **[S]** Phoenix Point's **return fire** is discussed by its own community as a
  contested subsystem — [BB3 Some thoughts on Return Fire — Snapshot Games
  forums](https://forums.snapshotgames.com/t/bb3-some-thoughts-on-return-fire/2207/16),
  [That return fire — Snapshot Games forums](https://forums.snapshotgames.com/t/that-return-fire/429/23),
  [Classes and Skills (diff) — Phoenix Point wiki](http://wiki.phoenixpoint.com/index.php?title=Classes_and_Skills&diff=prev&oldid=7989).
- **[S]** XCOM 1994 / UFO:AI reaction fire has bug history — [Reaction fire bugs —
  UFO:AI forums](https://ufoai.org/forum/index.php/topic,8667.msg62529.html).
- **[S]** Tabletop-style opportunity/reaction attacks are documented publicly —
  [What are the rules for opportunity attacks? — ncesc.com](https://www.ncesc.com/gaming-pedia/what-are-the-rules-for-opportunity-attacks/),
  [Can I use a reaction and a bonus action? — ncesc.com](https://www.ncesc.com/gaming-pedia/can-i-use-a-reaction-and-a-bonus-action/).
  *Both are SEO content farms — cited only for the existence of the term; the numeric
  rules there must not be trusted.*
- **[S-]** A Hugo open-source library for reaction/opportunity-fire exists —
  [`opportune.h` — hugoif/library-contributions](https://raw.githubusercontent.com/hugoif/library-contributions/main/opportune.h).
- **[I]** This is the single most architecturally invasive requirement in the whole
  survey: **a reaction system means the scheduler must be able to suspend the current
  actor mid-action, run a nested resolution, and then resume or invalidate the
  suspended action.** Every model above needs this in some form — opportunity attacks
  (phase/IGOUGO), reaction fire (TU, WEGO), overwatch (XCOM 2), snap-to-action (Bolt
  Action), counterattacks (Fire Emblem). A scheduler that is a plain "for each unit, run
  its turn" loop cannot express it.

### 10.3 UI
- **[S]** Turn-order UI is a real, specified feature in multiple shipped/OSS projects: a
  JRPG-combat reference implementation documents a **turn bar** —
  [Turn Bar — godot-2d-jrpg-combat (DeepWiki)](https://deepwiki.com/gdquest-demos/godot-2d-jrpg-combat/3.2-turn-bar);
  a turn-based engine issue tracks a **turn-meter UI** —
  [feat: turn meter ui · Issue #28 · filipesantoss/quattuor](https://github.com/filipesantoss/quattuor/issues/28);
  a digital board game tracks an in-game multiplayer turn-order and player-list UI —
  [In-Game Multiplayer UI (Turn Order & Player List) · Issue #565 ·
  mage-knight-digital/MageKnight](https://github.com/mage-knight-digital/MageKnight/issues/565).
- **[S-]** An indie AI project tracks active-player highlighting and turn notifications —
  [Enhance Active Player Highlighting and Turn Notifications · Issue #101 ·
  jeffgabriel/eurorails_ai](https://github.com/jeffgabriel/eurorails_ai/issues/101).
- **[S]** A game mod explicitly visualizes impending enemy actions —
  [Enemy Action Visualizations — Skymods](https://catalogue.smods.ru/archives/361831).
  *Weak source (mod catalogue) but direct evidence that "show the enemy's next action"
  is a requested/shared UI capability.*
- **[S-]** Patent filings describe turn-order display in game UI —
  [US20040259634A1 (PDF)](https://patentimages.storage.googleapis.com/9a/ae/85/df411bbe901977/US20040259634A1.pdf),
  [US20070060226A1 (PDF)](https://patentimages.storage.googleapis.com/1b/63/ba/6d9ade31ed633a/US20070060226A1.pdf).
  *Legal documents — useful only as evidence that order display is considered
  patentable subject matter; **[U]** contents.*
- **[I]** UI consequences of each model (NOT verified):
  - CTB/initiative and ITB *require* a **queue projection / intent preview** API; without
    one the model is unplayable. FFX's displayed turn list is the classic example.
  - ATB requires a **continuous per-unit gauge rendering path** plus a visible
    active/wait policy indicator.
  - Phase-based requires a **phase indicator plus per-unit per-phase "already acted"
    state** to be surfaced.
  - WEGO requires an **order-review/confirm screen** and a **playback scrubber**, because
    the player cannot rewind the resolution otherwise.
  - IGOUGO needs the least order UI but the most "your turn is over" conflict
    management.

### 10.4 Multiplayer determinism
- **[S]** Deterministic lockstep is a recognized implementation strategy for
  turn-based/simultaneous games — [phaeton-forge/phalanx-engine: Deterministic lockstep
  multiplayer game engine — GitHub](https://github.com/phaeton-forge/phalanx-engine).
- **[S]** A commercial turn-based multiplayer engine exists as a middleware product,
  i.e. turn scheduling + networking is a sold capability —
  [SuperTiles Multiplayer — turn based engine (Unity Asset
  Store)](https://assetstore.unity.com/packages/tools/game-toolkits/supertiles-multiplayer-turn-based-engine-223973).
- **[S]** A general-purpose turn-system middleware asset exists for Godot —
  [xpTURN.Klotho — Godot Asset Store](https://store.godotengine.org/asset/xpturn/xpturn-klotho/).
- **[S]** Publisher-forum discussions of how multiplayer works and how randomness is
  handled in it — [How Does Multiplayer Work — Slitherine
  forums](https://www.slitherine.com/forum/viewtopic.php?p=707972),
  [Randomness in Multiplayer Games — Slitherine forums](https://www.slitherine.com/forum/viewtopic.php?p=445172).
- **[S]** Civ's multiplayer scheduling modes (Simultaneous vs Dynamic) are user-selectable
  — [Arqade question](https://gaming.stackexchange.com/feeds/question/347352).
- **[I]** Determinism requirements implied (NOT verified here): a turn scheduler is only
  netcode-safe if (a) the *order function* is a pure function of replicated state —
  i.e. all tie-breaks use a seeded, replicated RNG stream and stable entity ids, never
  floating-point comparisons of derived timers or hash-map iteration order; (b) the
  scheduler emits the same order on every peer given the same inputs; (c) any
  simultaneous-resolution step is either fully deterministic or its random draws are
  taken from a named, seeded stream consumed in a *scheduled* order (not per-unit
  iteration order). WEGO makes this hardest because resolution interleaves many actors'
  pre-committed intents inside one tick.

### 10.5 Architecture / testability resources found
- **[S]** A tabletop-to-digital engine keeps an explicit **turn lifecycle** architecture
  doc — [opencombatengine docs/architecture/007-turn-lifecycle.md —
  GitHub](https://github.com/jamesplotts/opencombatengine/blob/93f00746f1998d42d7ca9febb33260d2536af133/docs/architecture/007-turn-lifecycle.md).
- **[S]** An OSS roguelike library documents a **scheduling system** as a first-class
  module — [Scheduling System — RogueSharp documentation (raw
  markdown)](https://raw.githubusercontent.com/FaronBracy/RogueSharp.Documentation/refs/heads/main/articles/15_scheduling_system.md).
- **[S-]** A project issue for a turn-based roguelike is literally titled "Implement
  TurnManager system for turn-based phase control" —
  [qwang06/rogue-tbs issue #30](https://github.com/qwang06/rogue-tbs/issues/30).
- **[S-]** A Godot turn-system skill/reference documents turn models generically —
  [godot-master/references/turn-system.md — GitHub](https://github.com/thedivergentai/gd-agentic-skills/blob/HEAD/skills/godot-master/references/turn-system.md),
  [godot-turn-system/SKILL.md — GitHub](https://github.com/thedivergentai/gd-agentic-skills/blob/main/skills/godot-turn-system/SKILL.md).
- **[S]** Board Game Arena's own documentation asks "Why a game state machine?", i.e. the
  state-machine framing is the platform's recommended model for turn-based games —
  [Why a game state machine? (PDF, BGA docs)](http://en.doc.boardgamearena.com/images/9/98/4-gamestates.pdf).
- **[S-]** A LÖVE forum thread on storing/executing "between turn" logic — evidence that
  the *between-turn / end-of-turn resolution* step is a recurring implementation pain
  point — [LÖVE forums](https://www.love2d.org/forums/viewtopic.php?t=96303).
- **[S-]** An academic PhD thesis that touches turn/phase control in a strategy-game AI
  context — [Goudoulakis PhD thesis (LJMU Research Online,
  PDF)](http://researchonline.ljmu.ac.uk/id/eprint/4512/1/157941_2014GoudoulakisPhD.pdf).

---

## 11. Runtime capabilities implied for a complete TBS engine

Each row: capability → the source that motivated it. Confidence tags as in §0.

| # | Capability | Motivated by |
|---|---|---|
| 1 | **Turn-budget / action-point model** (a turn is not atomic; N actions per activation) | XCOM 2 action-point ambiguity [S] — [Steam discussion](https://steamcommunity.com/app/268500/discussions/0/366298942110338752); XCOM 1994 TU manual [S] — [UFO:AI Time Units](https://ufoai.org/w/index.php?title=Manual/Singleplayer/Time_Units/v2.5&feed=atom&action=history) |
| 2 | **Scheduler/queue service with pluggable ordering policy** (IGOUGO vs initiative vs phase vs WEGO behind one interface) | Civ ships two scheduling modes [S] — [Arqade](https://gaming.stackexchange.com/feeds/question/347352); BattleTech's official *alternatives* thread [S] — [battletech.com forums](https://battletech.com/forums/index.php/topic,54827.msg1267177.html) |
| 3 | **Priority-queue initiative resolver + pure "compute next" function** | FFT CT [S] — [Game8](https://game8.co/games/Final-Fantasy-Tactics/archives/543470); priority-queue batch scheduling paper [S] — [ITB PDF](https://informatika.stei.itb.ac.id/~rinaldi.munir/Matdis/2024-2025-2/Makalah2025/Makalah-Matdis-2025-IF-ITB%20(22).pdf) |
| 4 | **Re-derivable queue projection exposed to UI** (show future order, and recompute it after each action) | FFX CTB turn order [S] — [Gamer Guides](https://www.gamerguides.com/final-fantasy-x-hd/guide/introduction/gameplay/battle-system); [Turn Bar UI](https://deepwiki.com/gdquest-demos/godot-2d-jrpg-combat/3.2-turn-bar) [S]; [turn meter UI issue](https://github.com/filipesantoss/quattuor/issues/28) [S] |
| 5 | **Speed/priority stat that feeds the order function, and a delay/haste effect that can reorder the schedule mid-round** | FFT CT [S] — [Game8](https://game8.co/games/Final-Fantasy-Tactics/archives/543470); FFX "Rank" [S-] — [Fandom](https://finalfantasy.fandom.com/wiki/Rank_(Final_Fantasy_X)); Battle Brothers "staggered" reordering [S] — [forums](http://battlebrothersgame.com/forums/topic/necrosavants-still-act-first-when-staggered/); Fell Seal speed threads [S] — [Steam](https://steamcommunity.com/app/699170/discussions/0/3425571923627145601/) |
| 6 | **Real-time clock driver + explicit pause/active policy** (only for ATB) | ATB "semi-real time … time bar" [S] — [ecured.cu](https://www.ecured.cu/index.php?title=Final_Fantasy&diff=581975&oldid=581926); FFVII Remake Classic Mode discussion [S] — [IGN](https://me.ign.com/en/ps4/165527/final-fantasy-7-remakes-classic-mode-wont-satisfy-purists) |
| 7 | **Phase state machine over subsets of units, with per-unit *per-phase* "already acted" flags** | 40k phases [S] — [Warhammer Community](https://www.warhammer-community.com/en-gb/articles/W1JVMEow/the-new-edition-of-warhammer-40000-makes-all-the-phases-count/); machine-readable `Phase` type [S] — [40kdc-data](https://raw.githubusercontent.com/wn-mitch/40kdc-data/4e80d96efec2cbd1cc91c42ca3fccf1661a23656/tools/docs/api/generated/type-aliases/Phase.md); BGA "why a game state machine" [S] — [PDF](http://en.doc.boardgamearena.com/images/9/98/4-gamestates.pdf) |
| 8 | **Order-selection / activation-eligibility layer separate from the scheduler** (which unit *may* act next, and why) | Bolt Action order dice [S] — [Slitherine dev diary](https://www6.slitherine.com/news/bolt-action-dev-diary-3-order-die-unit-activations-and-snap-to-action); activation roundtable [S] — [Goonhammer](https://www.goonhammer.com/goonhammer-historicals-activation-roundtable/) |
| 9 | **Interrupt / nested-resolution stack** (suspend an actor mid-action, resolve a reaction, resume or invalidate) | Reaction fire designer essay [S] — [Delta Vector](http://deltavector.blogspot.com/2015/04/game-design-39-reaction-moves-reaction.html); Phoenix Point return fire [S] — [Snapshot forums](https://forums.snapshotgames.com/t/that-return-fire/429/23); UFO:AI reaction-fire bugs [S] — [forums](https://ufoai.org/forum/index.php/topic,8667.msg62529.html); `opportune.h` [S-] — [GitHub](https://raw.githubusercontent.com/hugoif/library-contributions/main/opportune.h) |
| 10 | **Out-of-band group activation / mid-phase insertion of a new actor set** (pod activation) | XCOM 2 pod activation bugs [S] — [Pavonis forums](https://www.pavonisinteractive.com/phpBB3/viewtopic.php?p=28639); pods discussion [S] — [Steam](https://steamcommunity.com/app/268500/discussions/0/598523169276476233); Alien Forces page [S-] — [StrategyWiki](https://strategywiki.org/wiki/XCOM_2/Alien_Forces) |
| 11 | **Enemy-intent objects published during the player phase and replayed deterministically in the enemy phase** | Into the Breach GDC postmortem [S] — [GDC Vault](https://gdcvault.com/play/1026333/-Into-the-Breach-Design); ITB AI PDF snippet [S] — [archive.org](http://archive.org/download/into-the-breach-ai/Into%20the%20Breach%20AI.pdf); enemy action visualizations [S] — [Skymods](https://catalogue.smods.ru/archives/361831) |
| 12 | **Two-phase write/resolve scheduler with order devalidation** (WEGO) | WEGO Overlord guide [S] — [Matrix PDF](http://ftp.us.matrixgames.com/pub/WEGOWorldWarIIOverlord/WEGO%20Overlord%20Quick%20Guide%20printer-friendly.pdf); Combat Mission manual [S] — [Matrix PDF](http://ftp.eu.matrixgames.com/pub/CombatMissionRedThunder/CMRedThunderManual3.01.pdf); Frozen Synapse as realtime/turn hybrid [S] — [Famitsu GDC 2012](https://www.famitsu.com/news/201203/14011482.html) |
| 13 | **Resolution playback / scrubber + order review UI** (WEGO) | BSG Deadlock reviews [S] — [IGN](https://www.ign.com/articles/2017/12/13/battlestar-galactica-deadlock-review-2), [Wikipedia](https://en.m.wikipedia.org/wiki/Battlestar_Galactica_Deadlock); Famitsu Frozen Synapse framing [S] |
| 14 | **Stable, replicable tie-break + named seeded RNG streams for the order function** | Determinism is a named engine property in lockstep engines [S] — [phalanx-engine](https://github.com/phaeton-forge/phalanx-engine); Slitherine "Randomness in Multiplayer Games" [S] — [forums](https://www.slitherine.com/forum/viewtopic.php?p=445172); Civ simultaneous-turns multiplayer [S] — [Arqade](https://gaming.stackexchange.com/feeds/question/347352) |
| 15 | **Turn/round lifecycle events and an explicit between-turn resolution step** | opencombatengine turn-lifecycle doc [S] — [GitHub](https://github.com/jamesplotts/opencombatengine/blob/93f00746f1998d42d7ca9febb33260d2536af133/docs/architecture/007-turn-lifecycle.md); LÖVE "between turn" thread [S-] — [forums](https://www.love2d.org/forums/viewtopic.php?t=96303) |
| 16 | **Per-unit multi-stat + action-cost table** (a turn's *cost* is data, not code) | FFT CT [S] — [Game8](https://game8.co/games/Final-Fantasy-Tactics/archives/543470); Press Turn turn-icon economy [S] — [Game8 SMT V](https://game8.co/games/Shin-Megami-Tensei-V/archives/348265); Battle Brothers dev blogs [S] — [tactical-combat-mechanics](http://battlebrothersgame.com/tactical-combat-mechanics/) |
| 17 | **AI/scheduler contract** (when may the AI think; what may it assume; how does it enqueue) | Improving AI in XCOM talk [S] — [Game Developer](https://www.gamedeveloper.com/design/video-improving-ai-in-i-assassin-s-creed-iii-xcom-warframe-i-); ITB AI PDF [S] — [archive.org](http://archive.org/download/into-the-breach-ai/Into%20the%20Breach%20AI.pdf); strategy/tactics AI module notes [S-] — [vasir repo](https://raw.githubusercontent.com/erikhazzard/vasir/refs/heads/main/.agents/skills/game-ai__architecting-ai/modules/strategy_tactics.md) |
| 18 | **Reusable turn-system middleware abstraction** (evidence the abstraction is productizable) | [xpTURN.Klotho — Godot Asset Store](https://store.godotengine.org/asset/xpturn/xpturn-klotho/) [S]; [SuperTiles turn-based engine — Unity Asset Store](https://assetstore.unity.com/packages/tools/game-toolkits/supertiles-multiplayer-turn-based-engine-223973) [S]; [RogueSharp Scheduling System](https://raw.githubusercontent.com/FaronBracy/RogueSharp.Documentation/refs/heads/main/articles/15_scheduling_system.md) [S] |

---

## 12. What I could NOT verify (explicit gaps)

All **[U]** items, consolidated — these are the things a working fetch path should close
first, in priority order:

1. **CT/CTB arithmetic.** FFX "Rank", FFT CT accumulation, tie-breaks, whether the queue
   is recomputed per action. Sources to fetch:
   [Gamer Guides FFX battle system](https://www.gamerguides.com/final-fantasy-x-hd/guide/introduction/gameplay/battle-system),
   [Fandom: Rank (FFX)](https://finalfantasy.fandom.com/wiki/Rank_(Final_Fantasy_X)),
   [Fandom: FFX battle system](https://finalfantasy.fandom.com/wiki/Final_Fantasy_X_battle_system),
   [Game8 FFX CTB](https://game8.co/games/Final-Fantasy-X/archives/270672),
   [Game8 FFT CT](https://game8.co/games/Final-Fantasy-Tactics/archives/543470),
   [altema FFT](https://altema.jp/fft/battlesystem),
   [Giant Bomb CTB concept](https://giantbomb.com/wiki/Concepts/Conditional_Turn_Based_Battle).
2. **ATB pause policy per title** (Active vs Wait defaults), gauge speeds.
   [RPS ATB origin piece](https://www.rockpapershotgun.com/final-fantasy-ivs-active-time-battle-system-was-inspired-by-race-cars),
   [ecured ATB description](https://www.ecured.cu/index.php?title=Final_Fantasy&diff=581975&oldid=581926).
3. **WEGO resolution semantics.** Combat Mission manual and WEGO Overlord guide were
   identified but not read — these are the highest-value unread primaries:
   [CM Red Thunder manual](http://ftp.eu.matrixgames.com/pub/CombatMissionRedThunder/CMRedThunderManual3.01.pdf),
   [WEGO Overlord quick guide](http://ftp.us.matrixgames.com/pub/WEGOWorldWarIIOverlord/WEGO%20Overlord%20Quick%20Guide%20printer-friendly.pdf).
4. **XCOM 2 pod-activation rules** (radius/LOS trigger, scampers, chain activation) and
   the in-side action ordering. [Turn (XCOM 2) wiki](https://xcom.fandom.com/wiki/Turn_(XCOM_2)),
   [Alien Forces](https://strategywiki.org/wiki/XCOM_2/Alien_Forces),
   [concealment guide](https://www.gry-online.pl/poradniki/xcom-2/faza-ukrycia-concealment/z314329).
5. **Into the Breach enemy-phase ordering and intent model** — the GDC Vault talk and the
   AI PDF are the primaries: [GDC Vault postmortem](https://gdcvault.com/play/1026333/-Into-the-Breach-Design),
   [ITB AI PDF](http://archive.org/download/into-the-breach-ai/Into%20the%20Breach%20AI.pdf),
   [Game Developer talk coverage](https://www.gamedeveloper.com/design/video-how-subset-games-designed-i-into-the-breach-i-).
6. **BattleTech initiative brackets + bidding**, and whether HBS 2018 reproduces the
   tabletop exactly: [Quick Start Rules](https://battletech.com/qsr/),
   [Salvage Box QSR PDF](https://bg.battletech.com/wp-content/uploads/1645/44/Salvage-Box-Quick-Start-Rules.pdf),
   [initiative thread](https://battletech.com/forums/index.php?topic=73845.0),
   [alternatives thread](https://battletech.com/forums/index.php/topic,54827.msg1267177.html).
7. **Warhammer 40,000 phase list per edition** — the two official PDFs were found but not
   read: [40k rules PDF](https://warhammer40000.com/wp-content/uploads/2023/06/x8HeGjzdBWm5haRZ.pdf),
   [Warhammer Community PDF](https://www.warhammer-community.com/wp-content/uploads/2020/07/n7V0DjD9ja1DoQ1A.pdf),
   [40kdc `Phase` type](https://raw.githubusercontent.com/wn-mitch/40kdc-data/4e80d96efec2cbd1cc91c42ca3fccf1661a23656/tools/docs/api/generated/type-aliases/Phase.md).
8. **Bolt Action order-dice mechanics** (draw, snap-to-action, six orders):
   [Slitherine dev diary](https://www6.slitherine.com/news/bolt-action-dev-diary-3-order-die-unit-activations-and-snap-to-action),
   [Tech Times](https://www.techtimes.com/articles/321381/20260723/bolt-action-pc-reveals-gameplay-six-orders-suppression-come-life.htm).
9. **Civilization single-player turn processing** (does the human's order batch resolve
   before or interleaved with AI turns?) and the exact Simultaneous/Dynamic distinction:
   [Arqade](https://gaming.stackexchange.com/feeds/question/347352),
   [Shacknews Civ V MP preview](https://www.shacknews.com/article/65521/civilization-5-multiplayer-preview),
   [Civ VI simultaneous-turns thread](https://steamcommunity.com/app/289070/discussions/4/135507548127169842/).
10. **Keith Burgun's and Sirlin's actual arguments** (currently paywalled/unread):
    [Clockwork Game Design ch.2](https://www.taylorfrancis.com/chapters/mono/10.4324/9781315756516-2/theory-keith-burgun?context=ubx&refId=d6b81122-a4c3-4b0f-96ca-4bd492968e4c),
    [ch.4 Pitfalls](https://www.taylorfrancis.com/chapters/mono/10.1201/9781003484547-4/pitfalls-keith-burgun?context=ubx&refId=6bb8650d-ad09-4790-a249-3b745a22257c),
    [Sirlin Codex diary](http://oldsite.sirlin.net/blog/2014/3/25/codex-design-diary-a-rocky-road.html?printerFriendly=true).
11. **The CTU Prague thesis and the IEEE paper** — only titles/snippets seen:
    [CTU thesis](https://dspace.cvut.cz:443/bitstream/handle/10467/107001/F3-BP-2022-Boburka-Viktor-Bakalarska%20prace%20Viktor%20Boburka.pdf?sequence=-1&isAllowed=y),
    [IEEE Xplore](https://ieeexplore.ieee.org/document/11420395/keywords).
12. **Fire Emblem / Advance Wars / Wargroove exact within-turn rules**:
    [FE Turn wiki](https://fireemblemwiki.org/w/index.php?title=Turn&diff=269503&oldid=255190),
    [FE Fandom Turn](https://fireemblem.fandom.com/wiki/Turn),
    [Advance Wars archive](https://webarchiveweb.wayback.bac-lac.canada.ca/web/20051215000000/http://en.wikipedia.org/wiki/Advance_Wars),
    [Wargroove GDC-adjacent talk](https://www.gamedeveloper.com/design/watch-chucklefish-s-ceo-and-tech-director-discuss-i-wargroove-s-i-development).
13. **A GDC talk specifically on turn order / initiative.** None was found. The closest
    GDC hits were the [Into the Breach Design Postmortem](https://gdcvault.com/play/1026333/-Into-the-Breach-Design)
    and [Video: Improving AI in AC3, XCOM, Warframe](https://www.gamedeveloper.com/design/video-improving-ai-in-i-assassin-s-creed-iii-xcom-warframe-i-).
    **[U]** whether a dedicated "turn order" GDC talk exists — worth a targeted Vault
    search with a working fetch path.

---

## 13. Source-quality notes (so weak citations are not laundered)

**Strong / primary-ish found:**
GDC Vault Into the Breach postmortem; the Into the Breach AI PDF (developer-written);
`gamedeveloper.com` design articles and video posts; Slitherine/Bolt Action dev diary;
Battle Brothers developer blog; Warhammer Community official articles + official rules
PDFs; battlefront/Matrix official manuals and quick guides; BattleTech official quick-start
rules; Giant Bomb *concept* pages; Fire Emblem Wiki; Game8/Gamer Guides mechanic guides;
virt10 (Chalmers) design-pattern catalogue; Taylor & Francis *Clockwork Game Design*
chapters; IEEE/ITB/CTU academic items; the tabletop mechanism encyclopedia (TRN-02).

**Moderate:** Fandom wikis, StrategyWiki, Steam guides/discussions, Slitherine & Matrix
& Pavonis & Snapshot & Sirlin forums, RPS/IGN/Kotaku/RPG Site press, GitHub project docs.

**Weak — used only for existence of a term, never for a numeric/behavioural claim:**
GameSpot *user reviews* (several appeared repeatedly for FFX, XCOM, Advance Wars, Fire
Emblem, Civ V, Valkyria Chronicles); `ncesc.com` "Gaming Pedia" (SEO farm);
`icd-11.org` mirror of a Final Fantasy article; `gamerscout.io`; mod catalogue pages
(smods.ru); `gameres.com`/`gameloop` re-posts; the various news mirrors
(`tech.yahoo.com`, `newsbreak.com`, `me.ign.com`, `sea.ign.com`, `ign.com.cn`,
`alpha.arkansasonline.com`, `jikguard.com`). The patent PDFs and `pkg.go.dev` match were
incidental and are not relied on.

**Explicitly not used as evidence of anything:** the `muckrack.com` transcript fragment,
the `mastodon.gamedev.place` result, the local-filesystem-looking paths that appeared in
one result list (`/hdd/m0103/...`), and the Pioneer DJ `DDJ-WeGO` PDF (a false positive on
the string "WEGO").

---

*Prepared as part of the T2a gap-analysis pass. Evidence grade: title/snippet only —
no cited page body was retrieved in this session.*
