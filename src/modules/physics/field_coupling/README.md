# Explicit point-gravity rigid coupling

`physics_field_coupling` is a default-OFF L5 physics satellite depending on
`physics` and `physics_field`. It does not add a boot service or change old
world gravity, script entry points, body integration or saved formats.

`PointGravityBatch` observes selected active dynamic body masses and centers in
SI units, computes the immutable point field, and stages existing ForceBatch
loads. Commit recaptures the selected observations and rejects changed world,
tick, handle, mass or center before any load is applied. Each successful batch
can be submitted once. World3D remains the sole integrator; call its existing
step with the prepared tick/duration. Existing local gravity remains additive.

No body/world pointer survives preparation or submission. Runtime links become
stale after destruction or restore. Dropping a candidate cancels unsubmitted
work; moving transfers its right. Published loads follow the existing receipt
and failed-backend-step retry semantics. A same-tick velocity change does not
invalidate instantaneous position-dependent gravity; a position/mass change
does. A value changed and restored exactly is equivalent for this point model.

The four connection axes select explicit direct calls: one world owner knows
the point solver, calls it once per chosen step, and publishes atomically. The
public header forward-declares providers and exposes only owning results. The
six outward faces contain direct C++ calls and typed PhysicsLink inputs; no new
capability registration, ECS System, events, script bindings or MCP tools exist.
No registry or alternate simulation authority is introduced.

The preparation cost is field construction/evaluation plus O(B+N) for world
observation and load staging; commit is O(B+N). The point solver retains its
explicit work/range/error limits and may reject. No per-body tree traversal,
distance cutoff, implicit dt, wall clock or random stream is added. Numerical
loads that overflow or underflow to zero before/during conversion to the existing
float-force backend reject before submission. Float quantization and solver
integration errors remain separate from the field's analytic truncation bound.
Determinism is tolerance-based, not cross-backend bitwise equality. Candidate
rights are transient and must never be serialized; reconstruct after restore.

This is a point-mass, local-SI coupling. It is not finite-volume gravity, a
rotating/non-inertial frame adapter, or a million-body 60 FPS claim. Those remain
in the architecture implementation plan. Default-OFF selection and actual
enabled-build tests must be reported separately from full trim/compatibility
acceptance.
