# Script ECS

[中文](../../../usr/modules/entity.md) · [Module handbook](../MODULES.md)

Define data with `eve.Component`, compose it with `eve.Entity`, and update matching entities with `eve.System`. Keep data in components and behavior in systems.

```squirrel
class Position extends eve.Component { x = 0.0; y = 0.0; }
class Actor extends eve.Entity { pos = Position; }
local actor = Actor.create();
actor.pos.x = 100;
```

## Moving entities

Create separate Position and Velocity components, combine them in an Entity class, and derive a movement system from `eve.System`. Iterate matching `entities()` during update. Your `eve_update` determines system execution order explicitly.

Start with the runnable [ECS example](../../../../examples/ecs/main.nut).

## GPU systems

`eve.ShaderSystem` extends System. It packs float fields into GPU buffers and dispatches compute work during update:

```squirrel
local sys = eve.ShaderSystem(Moveable, gpgpu, moveGlsl)
sys.bindFields(0, "pos", ["x", "y"])
sys.bindFields(1, "vel", ["x", "y"])
```

`Moveable` and `moveGlsl` are your entity definition and shader source. See [gpu_main.nut](../../../../examples/ecs/gpu_main.nut). For large simulations, supported range upload/readback and shared resident buffers avoid unnecessary full synchronization; consult that example and the [GPU guide (Chinese)](../../../usr/modules/gpgpu.md).

## Lifecycle

Queries return entities matching the component composition. Plan lifetime before retaining references. Prefer small components and systems over an all-purpose Actor. Do not hide side effects in component data. When hot reload replaces class definitions, migrate retained instances instead of mixing old and new layouts.

See [API conventions](../API-CONVENTIONS.md). The ECS base classes are script-defined, so the C++ reference alone does not describe all their methods. [Source](../../../../src/engine/common/ECS.cpp).
