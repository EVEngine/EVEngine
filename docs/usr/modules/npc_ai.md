# NPC AI

The `npc_ai` module is EVEngine's data-oriented orchestration runtime for authored
NPC behavior. It combines a hierarchical state tree with typed blackboards,
event-driven perception, bounded scheduling, task providers and reservable Smart
Objects. It intentionally does not introduce a universal actor base class.

## Behavior runtime

Create a `BehaviorDefinition`, declare its blackboard schema and states, then publish
it through `NpcAiWorld::registerBehavior`. Publication validates the entire graph
before changing the registry. Agents receive generation-qualified `AgentHandle`
values; resolve operations reject handles after destruction and slot reuse.

The active state is a leaf. Every state from the root to that leaf is active, so
parent tasks such as awareness or hit reaction can run concurrently with leaf tasks
such as patrol or attack. Transitions are evaluated leaf-to-root. A target's complete
enter-condition path is checked before any current task is stopped.

Task implementations derive from `ITaskService`. The world takes unique ownership of
each provider. Navigation, animation, combat and project-specific gameplay remain
outside the core and are connected as providers. Missing providers are detected
before callbacks; if a later task fails to start, tasks started during the same
admission pass are stopped with `StopReason::StartupRollback`.

## Perception and diagnostics

`remember` stores bounded `(subject, sense)` memories with confidence, payload and a
simulation-tick lifetime. Updating a memory queues `perception.<sense>`; expiry queues
`perception.forgotten.<sense>`. No wall-clock time is read by the runtime.

`snapshot` returns an owning copy of the active path, blackboard and perception state.
`trace` returns fixed-capacity lifecycle records suitable for an editor debugger or
automated gameplay diagnosis.

`archive` returns an owning `AgentArchive` with schema id/version and logical ids only.
`restoreAgent` validates the behavior, active path, blackboard types, perception budget
and task ownership before publishing a new generation handle. Incomplete task providers
restart from their opaque memory; process-local navigation tickets are never revived.

## Smart Objects

`SmartObjectWorld` registers world interactions such as cover points, doors, seats or
work stations. Queries filter free slots by activity, required tags, distance and
stable ordering. A successful claim returns a generation-qualified lease handle.

Call `releaseAgentClaims` before destroying an NPC. When an object is removed first,
choose `RejectClaimed` to preserve claims or `CancelClaims` to invalidate them
explicitly. `expire` provides bounded cleanup for abandoned leases.

## Navigation and ECS

`NavigationTaskService` adapts an async `INavigationProvider` to state tasks. A request
factory turns authored task parameters into a provider request; start returns without
blocking, tick polls the ticket, and transition/destruction abandons outstanding work.

`NpcAgentState` stores only a generation-qualified link to `NpcAiWorld`.
`NpcAiEcsSystem` checks links before the AI step and publishes an owning
`NpcAiProjection` afterward. It closes each ECS view before task callbacks run and does
not mutate entity structure during iteration. Call `releaseAgent` for entity-first
cleanup; world-first cleanup is detected as a stale link and clears safely.

## Determinism and threading

All mutation is simulation-thread-affine. Callers inject simulation tick and dt.
Agent scheduling is stable and round-robin within an explicit per-frame budget;
transition loops have a separate per-agent budget. Provider floating-point results
must document their tolerance contract.

The contract suite includes a 2,048-agent population advanced in 128-agent batches;
every agent receives progress after one complete round. This is a deterministic budget
proof, not a hardware performance benchmark.
