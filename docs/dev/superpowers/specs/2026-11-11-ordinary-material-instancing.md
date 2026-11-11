# Ordinary material instancing without a bindless texture table

Graphics owns this change. The RenderSystem3D producer calls the existing Graphics
backend boundary once per opaque shared-material/mesh batch. The native C++ face
adds a cheap support query and a checked synchronous batch draw; there are no new
capabilities, dependencies, script bindings, events, tool methods or persistent
formats. Unsupported backends report false/Unsupported and use individual draws.
This is an extension of the existing module-interface, not another registry.

The existing Renderable3D View still reads Transform3D and MeshRenderer once.
Collection/culling and shadow/G-buffer work remain authoritative. Only the forward
replay is grouped. No ECS writes or structural mutations occur. Matrices are
frame-local derived data. Groups require one material, mesh, default camera and
shadow-receive policy, static opaque geometry, no custom shader, skinning, fading,
clustered lighting or spatial probes. Other cases retain individual draws.

The mechanism follows R-MECH-1: growing cardinality, per-frame frequency, one known
backend, synchronous draw. It therefore uses the existing View and backend virtual
port, with no per-entity capability/string lookup. Hot paths declare eve-hot-path.
R-FACE-1/2: the changed native Graphics boundary is the only new face; all other
faces are unchanged. R-BOUND-1/3: minimal+font+profiler+gpgpu is the real L2 link
and L3 runtime composition used for validation, not an include-graph trim claim.

Vulkan reuses ordinary material descriptors. Vertex binding 21 contains either
skinning matrices or static instance matrices, selected by skinInfo.z; the two
uses are exclusive. Existing descriptor layout, fragment shading and sampler
counts are unchanged. CPU matrices are copied into draw-specific storage in the
current frame slot, retained through its fence and reused only after that fence.
Meshes/materials remain their original owners' authoritative resources. No caller
memory or entity pointers are retained by the batch API. Graphics teardown and
resize follow the existing palette-buffer and descriptor lifetime mechanism.

Draw failures use nodiscard Result and propagate to the frame error boundary;
invalid inputs are rejected before recording, with no individual-draw retry after
recording failure. R-COST-1/4: the bulk draw makes its linear upload explicit via
the matrix span and documents first-slot growth and amortized binding cost; it
converges on the existing drawMeshShaderRange implementation. No builder ceremony
is added to a normal render submission. Each batch is bounded to 16384 matrices.

This introduces no simulation time, RNG or persistent state. Opaque pixel parity
is tolerance-bounded: CPU versus GPU matrix products can change edge coverage;
regressions compare total error and a small edge-pixel budget, with a nonblank
reference requirement. Tests exercise per-draw snapshots, caller mutation, regular
draws after instancing, multiple frame-slot reuse, invalid matrices, and explicit
provider absence on WebGPU. Composition compares individual materials against a
shared material and verifies batching through the existing render tracer. Desktop
bindless eligibility and its original route are unchanged.
