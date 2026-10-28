# Developing and Reviewing a Module

Sources: [interface/mechanism specification (Chinese)](../../dev/模块接口契约与机制选型规范.md), [boundary checklist (Chinese)](../../dev/模块边界审查清单.md). [Developer index](README.md).

This practical companion translates the principal review rules. Stable rule IDs remain authoritative references; detailed gate implementation states and historical audit counts stay in the Chinese specifications.

## Declare the boundary

External exchanges belong to six faces:

1. Provided capabilities: name, implementation and observed absence behavior.
2. Consumed dependencies: required/optional capabilities and explicit fallback.
3. Data/relationships: ECS components or scene Links, owner, stale detection and destruction order.
4. Events: type, ordering, dispatch phase and reentrancy.
5. Script surface: eve classes/methods and ownership.
6. Tool protocols where applicable: MCP/LSP/DAP names and response shapes.

Declarations must be falsifiable against source/build/tests (`R-FACE-1..5`). Domains own their interfaces/state; common owns mechanisms and domain-neutral values/envelopes.

Architecture CI (`make check/architecture-contracts`) lints changed C++ lines only — api-shape, ownership/lifetime Doxygen on raw pointers, runtime lookup bans, hot-path primitives, `@cost`. There is **no** hand-maintained JSON catalogue. Mark hot paths with a leading `// eve-hot-path` comment, or a directory-local `.eve-hot-path` policy containing `eve.hot-path-directory/v1:*.cpp`. Design notes: [`docs/dev/superpowers/specs/2026-10-14-architecture-contracts-code-first.md`](../../dev/superpowers/specs/2026-10-14-architecture-contracts-code-first.md).

## Include dependencies and trimming

Includes measure rebuild radius and layering. Link dependencies come from undefined symbols, construction/destruction, non-inline calls and RTTI. Include graphs cannot prove link-time omission (`R-BOUND-1..4`).

State the tested layer: compilation without the module (L1), linking without its objects (L2), handled runtime absence (L3), or tested present/absent profiles (L4). Use symbol evidence or a real configure/build/link/start sequence for trimming claims. Name the profile.

Unused code is not proof of independence: object-library inputs and dead-code elimination can change the outcome. Test the actual linkage configuration.

## Derive the mechanism

For a new exchange, state cardinality, frequency, whether observers are known and atomicity (`R-MECH-1`).

- Per-entity every-frame data: ECS views.
- Process-level provider needed every frame: resolve once at startup, retain the dependency.
- Low-frequency ordered provider fallback: listeners and ordered traversal.
- State changes with unknown observers: Observer/events.
- Owned relationships: typed handles/Links with invalidation.
- Multi-step all-or-nothing changes: existing transaction/publication contracts.

Keep discovery in composition roots, adapters or explicit boundaries. Do not scatter absence branches in business logic or perform module/capability lookup per entity/frame. Declare real hot-path files; moving work outside declared globs is evasion. Do not add module-internal feature flags when the boundary should be another module (`R-MECH-2..6`).

## Mechanism and policy

Merge only code sharing an invariant, reason to change and owner (`R-COHESION-1`). Similar shape is insufficient. Reuse provider lifecycle mechanisms rather than inventing another registry; keep domain policy separate (`R-COHESION-3..4`).

The consumer owns the interface for its question; the provider implements it. For a cross-domain concept, first try a typed relationship, then a lower-level mechanism with consumer-specific interfaces. Create a new domain only with its own invariant, lifetime and owner that neither existing domain can own (`R-COHESION-5..6`).

## Visible cost and ownership

Expensive public operations need a concrete, current Doxygen `@cost`: relative scale, amortization and side effects (`R-COST-1..5`). Use distinct names or return/token shapes for cheap/expensive paths; default arguments must not silently choose another cost tier. Do not add ceremonial builders/options for cheap operations.

Public headers use Doxygen comments with brief/parameter/return and relevant failure/ownership/thread/lifetime details. Follow [Result contracts](RESULT-CONTRACTS.md) and [binding style](SCRIPT-BINDINGS.md). Handbooks describe tasks/binding differences; references provide signatures.

## Review evidence

```sh
python3 scripts/module_boundary_audit.py --capabilities
python3 scripts/module_boundary_audit.py --findings
python3 scripts/module_depgraph.py --check --check-layers
ARCHITECTURE_BASE=HEAD make check/architecture-contracts
make check
```

The audit is read-only evidence, not a gate substitute. Candidates need human PASS/FAIL decisions; unverified properties stay UNVERIFIED with missing evidence named. A full boundary review updates the Chinese ledger.

Report the boundary/owner, stable rule IDs, mechanism derivation, present/absent behavior, tests and limitations. Existing modules improve when touched; an old full-repository audit does not require fixing every module in one PR.
