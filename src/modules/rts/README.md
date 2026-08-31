# RTS composition profile

`rts` is EVEngine's deterministic high-level RTS composition layer. It does not
own a second economy, order queue, combat runtime, crowd solver, definition
registry, or pathfinder. Instead, RTS roots carry typed links and adapters to the
canonical engine modules, and `eve::rts::RTS` coordinates their fixed-step
systems.

## Current systems

- Unit, building, player, faction, resource-node, and match ECS roots with stable
  `SubjectRef` identities and generation-checked relationships.
- Selection command fan-out, queued orders, deterministic grid/line/wedge
  formations, canonical A* navigation, traffic priorities, and crowd avoidance.
- Construction, repair, capture, mining, worker allocation, production, rally
  points, transport, garrisoning, supply, power, income, research, and AI policy.
- Fog, radar/detection, automatic combat, weapon-role target selection, shared
  armor-aware damage commitments, projectiles, splash damage, suppression,
  artillery fire support, and safe shoot-and-scoot relocation.
- Automatic ammunition dispatch, reservations, multi-vehicle convoys, moving
  relay interception, hostile-coverage-aware rendezvous, convoy pacing, and
  convoy-wide escort screens.
- Match victory/surrender, canonical snapshots and hashes, command logs, replay,
  and lockstep coordination.

## Integration outline

Create roots through `eve::rts::RTS`, then install the game-owned provider
boundaries required by the scenario:

```cpp
eve::rts::RTS simulation;
simulation.setNavigationProvider(&pathfinder, navigationGrid);
simulation.setCrowdProvider(&crowd);
simulation.setCombatProviders(&sensing, &damage);
simulation.setResourceCredit(creditResources);

auto faction = simulation.newFaction(factionSubject);
auto worker = simulation.newUnit(workerSubject, workerDefinition);
auto minerals = simulation.newResourceNode(nodeSubject, "minerals", 1500.0f,
                                            {8.0f, 0.0f}, 3);

eve::action::ActionRuntime actions;
eve::rts::ActionAdapter actionAdapter(actions);
simulation.step(fixedStep, actionAdapter);
```

Provider objects remain authoritative. For example, `map::Pathfinder` owns path
search, `crowd::Crowd` owns local collision avoidance, `orders::OrderQueue` owns
order lifecycle, and `combat::DamageRuntime` owns damage prediction/application.
RTS state stores only its domain projections and typed links to those providers.

## Content and synchronization

`RTSContentLoader` atomically publishes RTS archetypes into the canonical
`definitions::DefinitionRegistry`; invalid references reject the whole import.
`snapshotState`, `restoreState`, and `rebuildState` serialize stable subjects and
rebind runtime handles after topology restoration. `canonicalStateJson` and
`stateHash` expose the deterministic synchronization projection used by replay
and lockstep checks. The self-contained script profile also provides named
in-memory checkpoints that restore root components, faction economies,
navigation costs and blockers, fog memory, pending production subjects,
command history, and the fixed simulation tick as one state boundary. When
production or destruction changed root topology, restore pre-validates the
stable graph, rebuilds the roots and provider links, and rolls back the live
graph if rebuilding fails.

## Legacy migration map

The former monolithic `RTSWorld` is migrated by behavior rather than by copying
its numeric-id state store. Stable `SubjectRef` identities replace legacy
process-local entity integers. The current ownership mapping is:

| Legacy behavior family | Composition authority |
| --- | --- |
| Movement, formations, queued commands | RTS order components plus `orders`; `map::Pathfinder` owns paths |
| Local avoidance and traffic | `crowd::Crowd` plus RTS generation-checked crowd links |
| Mining, resource spending and refunds | RTS worker policies plus caller-owned economy accounts |
| Construction, repair, capture and production | RTS building projections plus `building`, `production`, `transaction` and `action` |
| Visibility, radar and target acquisition | `map` FOV and `sensing::SensingWorld` |
| Damage, weapons and projectiles | `combat::DamageRuntime` and `weapon` runtimes |
| Teams and alliances | `Match::Participants` plus `FactionRelationSystem`; no parallel diplomacy table |
| Content, upgrades and persistence | `definitions`, RTS tech projection, stable snapshots and replay |

The Squirrel boundary creates and materializes data-defined roots by stable
`SubjectRef`, issues batched movement, attack, patrol, repair, capture,
garrison, transport, stance, traffic-priority and cloak commands, binds workers
to compatible resource nodes/dropoffs, controls deterministic idle-worker assignment,
configures faction-wide automatic construction/repair reserves and assigns additional builders,
adjusts authoritative weapon reserve ammunition and mobile/depot supply stock, toggles
automatic resupply with active-mission cleanup,
heals live unit/building combat roots and applies canonical data-defined status effects,
assigns finite or persistent suppression corridors and stable escort rings, queues
the same tactical commands at exact replay ticks, including appended movement,
attack-ground, stop, hold, patrol, repair, capture, garrison and transport orders,
queues construction/production/research,
casts abilities, advances a fixed step through a canonical `ActionRuntime`, and
returns owning `inspectState` and transient `inspectFrameEvents` projections for
UI, audio and debug rendering. Frame events observe committed weapon firing,
projectile launch, complete reload lifecycles, dry weapons, edge-triggered blocked fire and deterministic miss feedback alongside canonical damage
outcomes from abilities, projectiles and direct weapons; all three damage routes
also award hostile kill experience through one shared veterancy settlement. Events are cleared
at the next step boundary and intentionally excluded from snapshots and hashes. Its
self-contained lifecycle stream also reports suppression recovery, full shield recharge,
construction completion, blocked/cleared production exits and produced units through the
same ordered frame projection. Building capture, ability cast/channel transitions and
canonical status application/expiry use that stream as well; commands applied at a lockstep
tick retain their pre-simulation lifecycle events in the same frame. Ammunition production,
supply dispatch, relay transfer, recipient refill and supplier return are projected through
the same deterministic stream. Its
self-contained sandbox composes real Pathfinder, Crowd, SensingWorld,
DamageRuntime, DefinitionRegistry and EconomyLedger instances and routes RTS
systems through their normal provider callbacks. Native games can continue to
inject externally owned instances instead.

The script profile also owns one canonical `map::Fov` per faction and one
`RTSCommandLog`. Scripts can query visible/explored cells and stable last-known
contacts, schedule commands for exact future ticks, and import/export the same
portable command-log format used by native lockstep tooling. The sandbox renders
unexplored and remembered fog and no longer exposes live hostile roots through
its presentation layer. Explicit entity attacks and hostile targeted abilities
are authorized against the same current visible-and-detected contact projection;
point/ground-fire commands remain available for firing into fog. Navigation blockers also drive the default fire-line
and swept-projectile collision adapters: obstacle-aware hitscan cannot fire
through blocked cells, while blockers introduced after launch intercept a
projectile on its next swept segment. A finite per-cell terrain elevation field
feeds heightmap FOV and interpolated direct-fire profiles; data-defined units
and buildings provide independent muzzle and target heights. Terrain elevation
is script-readable, writable, and restored with named checkpoints. Linear and
ballistic projectiles launch at those absolute heights, expose both endpoint
heights to swept collision adapters, and perform three-dimensional target
impact tests; their vertical position and launch target height survive snapshot
restore. Match roots are likewise script-accessible through
stable identities: setup rules, teams, start, surrender, retained lifecycle
events and final outcomes all route through `MatchSystem`. The sandbox runs a
headquarters-destruction match rather than maintaining a parallel win condition.
Fog authorization has an explicit persisted `Faction::Intel::enabled` state;
an enabled faction with no contacts sees no targetable enemies, while native
integrations that deliberately omit a fog provider retain unrestricted entity
targeting. This avoids treating an empty first-contact state as disabled fog.

The migration intentionally does not preserve legacy getter-per-field and
selection-mutating APIs. `inspectState` is the owning query projection,
`commandUnits` is the stable-identity command boundary, and snapshot/replay use
the deterministic root serializers. This keeps one authoritative state path
while preserving the gameplay behavior of the former sandbox.
