# Stylize architecture

Stylize owns NPR definitions and user parameter instances. Graphics owns GPU resources,
render targets, and the execution of rendering commands. The dependency direction is
therefore `stylize -> graphics`; graphics must not include stylize headers.

## Object model

- `StyleDefinition` is immutable built-in metadata: techniques, required scene inputs,
  default injection stage, priority, and parameter schema.
- `StyleInstance` stores only validated parameter overrides. It creates a post pass or
  mesh shader from its definition without exposing frame-context uniforms as user knobs.
- `StylePass` is one executable full-screen operation carrying a `PostEffectDesc`.
- `StyleRecipe` compiles same-stage instances by priority and owns the executable passes.
  It asks Graphics for reusable transient canvases rather than requiring callers to
  manually manage ping-pong targets.
- `StyleChain` is the legacy, manually managed pass sequence and remains for compatibility.

The backend-neutral `graphics/PostEffect.h` contract is deliberately owned by graphics.
It defines insertion stages and input flags without depending on any effect module.

## Invariants

1. A capability is true only when the corresponding implementation exists. A known style
   is not automatically a post, mesh, or CPU style.
2. User parameter metadata has one source of truth and excludes render-context values such
   as time, texel size, and screen size.
3. A recipe cannot mix injection stages. A higher-level camera pipeline executes one recipe
   at each stage instead of pretending differently staged effects form a linear image chain.
4. Scene inputs are opt-in. Enabling a style must not force depth, normals, or motion vectors
   unless an executable pass actually consumes them.
5. Graphics owns shaders and canvases. Script-owned instances and recipes only retain
   non-owning handles covered by the Graphics lifetime.

## Extension path

Adding a built-in style requires one definition, parameter descriptors, and only the
techniques it actually supports. A future data-driven registry can replace the static table
without changing `StyleInstance` or `PostEffectDesc`.

The next pipeline integration step is a graphics-owned scheduler that accepts
`PostEffectDesc` providers at `afterOpaque`, `beforeTransparent`, `beforeTonemap`, and
`afterTonemap`. It should provide declared color/depth/normal/motion/object-ID inputs and
allocate transient resources from the frame graph. Stylize must provide descriptors through
the existing lower-layer interface rather than adding an upward graphics dependency.

Multi-input and temporal effects should extend `StyleRecipe` from a linear compiled recipe
to a constrained DAG with named inputs, resolution scale, and history resources. It should
not become a second general-purpose render graph.
