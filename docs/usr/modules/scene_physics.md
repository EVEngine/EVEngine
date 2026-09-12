# Scene Physics Composition

`scene_physics` is the 3D runtime composition satellite exposed as `scenePhysics`.
`bindGeneratedColliders(world3D)` routes later procgen collider publications into the
same caller-owned gameplay `World3D` used for stepping and queries. Scene and procgen
remain independent producers/consumers; the physics artifact provider owns only its
published bodies and shapes.

Binding requires an empty collider provider. Unbinding also requires no committed or
staged publications. Destroying the caller-owned world makes existing collider records
observably stale rather than retaining a raw world pointer. Clear publications before
an orderly unbind.
