# spatial_multiscale boundary

An opt-in L0 value-math satellite, disabled by default through
`EVENGINE_MODULE_SPATIAL_MULTISCALE`. `CellPosition.h` owns the public contract.
Calls are typed synchronous value operations: many callers may independently
project values, frequency is per explicit projection, the implementation is known
at compile time, and failure must publish no partial axis result. `FrameGraph.h`
adds an immutable static hierarchy with typed owner/generation frame handles.

The boundary exposes no capabilities, ECS components/Links, events, scripts or
tool protocol. It changes no old transforms or cameras. No pointers, callbacks,
global registries, clock or RNG are retained. Values belong to the caller; results
own their three-axis projection and error bounds. Normalization checks full-width
integer carries. Each frame graph exclusively owns its existing RuntimeObjectRegistry;
replacing/destroying a graph invalidates its identities, while moving transfers them.
Handles do not keep the graph alive. There is no stable persistence format,
dynamic frame migration, or scene lifecycle integration yet.

CellPosition inputs share an axis-aligned lattice within their reference frame;
FrameGraph explicitly relates differently oriented frames. Use IEEE binary64 with gradual
underflow and no fast-math/FTZ. Bounds cover arithmetic only. The numerical probes
are not evidence of complete engine trimming, infinite precision or full
rotating/dynamic reference-frame support.

FrameDefinition additionally accepts a row-major static local-to-parent rotation,
defaulting to identity. The root must stay exactly identity. Proper orthonormal
matrices are checked to 1e-12; reflections, shear, and nonfinite values reject.
The represented coefficients define the actual map; input orientation error is
not included in arithmetic bounds and matrices are never silently normalized.
Root-axis orientations are composed once at build time using outward-rounded
interval multiply/add. Position differences still cancel common ancestors before
conversion; each surviving term rotates in its own frame (translations use parent
axes). Identity-only paths retain the original arithmetic and bounds.

Projection remains O(depth), depth <=64, with fixed stack storage; construction
and storage remain O(number of frames). Interval dependency can widen bounds in
deep hierarchies; an insufficient requested budget returns Unsupported. This
provides static coordinate orientation, not Coriolis/centrifugal dynamics, angular
velocity, frame migration, or a time-dependent transform system. A new graph still
invalidates old handles. IEEE gradual underflow and no fast-math/FTZ are required.

`FrameGraph::reexpress` explicitly converts a position into another static frame
of the same graph. It returns a canonical `FramePosition` and per-axis arithmetic
uncertainty in destination units. The caller owns the result and must retain that
uncertainty. Same-frame conversion preserves full-width addresses; cross-frame
conversion cancels common ancestors, projects in destination-scale root axes and
applies an interval inverse of the represented destination orientation. It does
not assume that an approximately orthonormal matrix has an exact transpose inverse.
Range/precision failures publish no partial result. This is a typed synchronous
O(depth) value query, with no callbacks, ECS mutation, providers or retained pointers.
It does not migrate velocity, acceleration, rigid-body ownership or simulation time;
dynamic migration and non-inertial dynamics remain separate unfinished work.
