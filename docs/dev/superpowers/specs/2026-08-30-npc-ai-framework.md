# NPC AI framework architecture

Date: 2026-08-30

## Product target

The framework is an orchestration layer for authored, inspectable and scalable NPC
behavior. It follows the production patterns shared by hierarchical state-tree and
behavior-tree systems, data-oriented crowd simulation, event-driven perception and
reserved world interactions. It does not create a universal actor base class.

The primary reference is Unreal's production split: StateTree combines hierarchical
states, transitions and concurrent tasks; Mass keeps entity data separate from
processors and uses signals/LOD for scale; Smart Objects are queryable data plus
exclusive slot reservations rather than interaction logic. EVEngine adopts those
contracts without importing Unreal's Actor hierarchy or editor/runtime coupling.

## Runtime layers

1. `npc_ai` owns validated behavior definitions, agent runtime state, blackboards,
   signals, task lifecycles, deterministic scheduling budgets and diagnostics.
2. `sensing` supplies candidates and perception evidence; an adapter turns changes
   into blackboard facts and wake-up signals.
3. Navigation, `steering`, `crowd`, animation, action/combat and smart interactions
   implement narrow `ITaskService` providers. The core owns providers but never
   includes their domain headers.
4. Scene/ECS integration stores only `AgentHandle` links. Components never retain a
   pointer into the AI world and every use re-resolves generation.
5. Editor tooling authors versioned definitions, compiles them transactionally and
   observes snapshots/traces without becoming an authoritative runtime owner.

## ECS system declaration

`NpcAiEcsSystem` preflights the `NpcAgentState` generation links, closes the ECS view,
ticks the authoritative world, then opens a separate `NpcAiProjection` view. It makes
no structural mutation and never invokes task providers while a view is open. The
machine-readable read/write/service/phase declaration lives in
`scripts/architecture_contracts.json`.

## Determinism and scale contract

Simulation tick and dt are injected. Agent slots and transition selection have stable
ordering; equal transition priority uses authored order. The initial implementation is
deterministic for the same definitions, inputs and provider results. Floating provider
outputs are tolerance-bounded. The scheduler has explicit per-frame agent and
per-agent transition budgets and round-robin deferral. Async navigation tickets and
Mass-style signal wakeups are present; later tiers add significance LOD and batched
spatial/provider queries.

## Lifecycle and failure contract

Definitions validate completely before publication. Agent handles are process-local,
generation checked and stale after either destruction order. The world uniquely owns
task services. Task callbacks run on the simulation thread without an engine lock and
must not re-enter the world. Destroy/transition stops active tasks before state is
discarded. `AgentArchive` uses a stable schema id/version and logical behavior/state/task
ids; runtime handles never enter saves. Restore validates a complete candidate before
slot publication. Incomplete external tasks retain opaque task memory but restart the
provider, so a process-local ticket is never treated as live after load.

## Delivery phases

- Phase 1: core state tree, blackboard, signals, owned task providers, budgeting,
  snapshots and contract tests.
- Phase 2: hierarchical active paths, typed schemas, perception memory/forgetting,
  trace ring buffer and versioned logical archives.
- Phase 3: navigation/steering/crowd, animation/action/combat and smart-object
  reservation providers; scene/ECS composition tests and provider-absent tests.
- Phase 4: editor graph compiler/debugger, hot reload migration, save/replay and
  network authority contracts.
- Phase 5: significance LOD, batched/async queries, profiler counters, soak tests,
  thousands-of-agent benchmarks and playable stealth/combat showcase.

## Current implementation status

- Phases 1 and 2 runtime foundations are implemented: validated hierarchical state
  paths, typed blackboards, bounded perception, signals, task admission rollback,
  fixed trace storage, versioned logical archives and atomic restore.
- Phase 3 orchestration is implemented: Smart Object spatial/tag queries and leases,
  async navigation tickets with abandonment, generic owned task-provider contracts
  for animation/action/combat, and generation-safe ECS composition with both
  destruction orders. Concrete animation/combat policies remain domain providers.
- Phase 5 has bounded-population evidence: a 2,048-agent contract test advances 128
  agents per step and proves round-robin progress. Significance LOD, batched providers,
  soak/performance timing and a playable showcase remain follow-on product work.

## Reference material

- Unreal Engine StateTree overview:
  https://dev.epicgames.com/documentation/unreal-engine/overview-of-state-tree-in-unreal-engine
- Unreal Engine MassEntity overview:
  https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-entity-in-unreal-engine
- Unreal Engine Smart Objects overview:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-objects-in-unreal-engine---overview
