# physics_field boundary

An opt-in L0 numerical satellite. Its outward API is defined by `GravityField.h`
and `GravitySimulation.h`, not a parallel interface metadata catalogue.

The connection mechanism is a direct typed batch call: one caller owns each
snapshot/simulation, calls are frequent, the consumer knows the provider at
compile time, and publication must be atomic. This does not justify a global
capability lookup, event bus, or string-keyed lookup per particle.

The six boundary faces currently are:

- Capability input/output: none; direct value and owning-result C++ APIs.
- ECS/Link: none. No entity/component address is retained. Point state is owned
  by `GravitySimulation`; a rigid-body adapter must not independently integrate
  the same state.
- Events: none; prepare/commit returns synchronous typed outcomes.
- Script: none; old `eve.physics` bindings and boot behavior are unchanged.
- Tool protocol: none; no MCP registration.

Field build owns copied sources, coefficient trees and interaction lists. Stage B
returns independent samples and validates the complete tick/revision. Concurrent
const evaluations are allowed; moving or destroying the field concurrently is
not. The hot path uses typed indices and fixed-degree monomials, no callbacks.
Each target node tracks its local error charge plus the maximum charged path
below it. A candidate interaction checks that subtree maximum and all ancestor
charges before accepting. Direct pairs spend no analytic truncation budget.
Updating ancestors costs O(depth) per far interaction, with depth explicitly
bounded. Allocation/order of error budget is deterministic for the input tree;
it need not minimize work and does not guarantee admission of arbitrary inputs.
Kernel polynomial scratch is fixed-size stack storage; successor indices are
precomputed per basis instead of per interaction. The remainder estimate still
excludes roundoff and physical-model error.

Simulation operations are serialized by the owner. Prepare leaves the published
state untouched; commit consumes a candidate and checks owner identity plus base
stamp. Candidates do not keep their owner alive. Destroying either side first is
safe. Moving the simulation transfers the owner identity; replacing it revokes
old candidates. There is no persistent schema or restore API yet: runtime stamps
and candidates must not be serialized as stable external identities.

Determinism is numerical for identical input/order/build. Cross-backend bitwise
identity is not promised. No clock, unnamed RNG or implicit dt enters the API.
All ranges and resource budgets are explicit; rejection never silently drops
sources, targets, or elapsed simulation time.

`GravityField::buildAtTargets` accepts independent source and target spans. Sources
must be nonempty positive masses; targets may be empty, repeated, or in a different
region. Inputs are copied. Separate source and target trees share normalization;
only source rows contribute mass. `metrics().particles` remains the source count,
and `metrics().targets` reports output count. Evaluation preserves target input order.
For this entry point `maxParticles` bounds Ns+Nt, so one million separate sources
and one million targets require an explicit two-million-row budget. Node and
interaction limits include both trees. Exclusion accounting is O(Nt+P) for admitted
direct pairs P, in addition to the documented tree/translation work.

A target may exclude one source by original input index only when their positions
are exactly equal. This models self interaction, not arbitrary masking of distant
sources. Coincident pairs without that exclusion remain singular and reject at
evaluation. The source-order rank map handles exclusions after spatial partitioning;
excluded pairs do not consume the direct-pair budget. The original `build` entry
retains its single-tree source-as-target path and implicit self-index exclusion.
This extension adds no provider, event, ECS state, persistence format or script path.

`EVENGINE_MODULE_PHYSICS_FIELD=OFF` is the default. Standalone numeric/static/DLL
probes establish only the tested boundary; full engine trim and provider adapter
claims remain pending. See the architecture implementation plan and validation
record under `docs/dev/` for outstanding integration and acceptance work.
