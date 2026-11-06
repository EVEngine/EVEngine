# procgen_recursive boundary

Optional L0 numerical satellite, disabled by default. The existing Procgen
module, scripts, seeds, generated artifacts and caches are unchanged.
`RecursiveModel.h` defines its public value/owning-result contract.

The initial rule is a conservative binary volume partition. Each split decreases
potential leaf count, so termination/progress is structural; it cannot invoke a
user rule that loops. Capacity and physical length exponent are independent.
Fixed-volume capacity makes smaller partitions; fixed-mean-leaf-volume capacity
grows root volume while preserving nominal mean leaf volume. This is a source
summary substrate, not an implementation of ship or city meshes.

Analytic mass, centroid and bounds do not require descendant enumeration. The
source frontier must cover every potential leaf once, including coarse residual
siblings of refined regions. Parent/child overlap, duplicate nodes and holes are
rejected before returning an owning frontier. Preparing a frontier costs
O(K depth + K log K), explicitly outside any claimed per-frame linear field loop.
Numerical conservation is tolerance-based; a certified geometry/roundoff error
budget is still pending. `RecursiveInstance.h` adds a sole-owner sparse ledger,
staged revision-checked publication, and bottom-up edited ancestor summaries.
Mass, displacement and velocity overrides survive changes in active refinement;
coarse source covers cannot erase the ledger. Destroying candidates cancels;
destroying an owner or superseding a revision revokes publication rights.
Edited summaries also retain central second mass moments (xx, yy, zz, xy, xz,
yz), divided by nominal root mass in root-local length squared. Uniform leaves
contribute their finite-volume diagonal moments; ancestors use the parallel-axis
merge, including mass edits and translations. Inertia follows trace(Q)I-Q.
Overflow or lost positive merge weights reject preparation. These are derived
caches rebuilt on restore, not another persistent authority. The current point
field solver does not yet consume these finite-volume moments.

The four connection axes select direct typed calls: callers know this rule,
query it frequently, each model exclusively owns one immutable definition, and
frontier preparation returns all-or-nothing. No global provider or string lookup
occurs per generated node. Const reads are reentrant; destruction/moves require
exclusive ownership. Dropping a prepared frontier cancels its use without side
effects. No callbacks, wall clock or stateful RNG are involved.

The outward faces currently contain direct C++ APIs only: no capabilities,
ECS/Links, events, script bindings or tool protocol. Stable NodeKey paths belong
to a definition; they are not runtime object handles. Random words depend on seed,
rule version, path, named stream and counter. The versioned rule rejects unknown
versions. `RecursivePersistence.h` seals the authoritative definition and sparse
ledger through the existing common SnapshotEnvelope, schema `eve:recursive-model`
version 1. Unknown fields/versions are rejected; no predecessor migration exists.
The host supplies persistent identity, simulation tick and digest provider.
Restore validates integrity and resource limits, rebuilds summaries, and returns
a fresh owner. Only a successful host move publishes it; old candidates cannot
publish into that owner. Hash callbacks run synchronously after capture, without
locks; provider absence is an explicit failure. No filesystem or UI is added.
Engine save integration, adapter transactions, leaf generators and rendered
examples remain to be implemented.
