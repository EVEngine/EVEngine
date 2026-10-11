# API Conventions

[中文](../../dev/API-CONVENTIONS.md) · [User guide](README.md)

This page is maintained by hand; the Chinese documentation is authoritative.

## Modules and objects

Script constructors live in the global `eve` table. Constructors such as `eve.Physics()` and `eve.Audio()` create module objects. Their factories create helper objects such as worlds, bodies and audio sources. Call a method on its owning object, not on the module merely because it appears in the same guide.

```squirrel
local physics = eve.Physics();
local world = physics.newWorld(0, 980, true);
local body = world.newBody("dynamic", 0, 0);
```

The startup environment creates common global instances. Explicit project module lists can limit them. Reuse the injected instances when appropriate instead of recreating long-lived modules each frame.

## Where to look up an API

The handbook explains workflows, examples, ownership and failure handling. Method lists are not copied into it.

- **C++ declarations and contracts:** the [generated Doxygen reference](https://evengine.github.io/EVEngine/), especially `@param`, `@return` and `@throws`.
- **Callable EveScript methods:** your SDK's `share/eve/ai/eve-api.json`, editor completion/hover, or MCP `eve_api_search` / `eve_api_get`.
- **Source checkout:** bindings in `expose(...)` / `addFunc` / `addVar`, script-injected classes, and the module's tests. To generate the script catalog locally, use `python3 scripts/generate_binding_contracts.py --json-output eve-api.json`.

An exposed C++ public method is not automatically an EveScript method. C++ default arguments are not automatically script defaults. Use positional arguments when the binding signature is unknown or ambiguous.

## Errors and results

Read each method's documented contract before calling it.

- Programming mistakes and failed preconditions, such as invalid choices, out-of-range indices or uninitialized backends, throw exceptions. Script bindings report them as Squirrel exceptions. Fix them rather than silently swallowing them.
- Recoverable failures may return `false`, `null` or a structured Result, depending on the API. Always check the documented return value.
- For Result tables, check `result.ok` before using `result.value`. Inspect `status.summary` / `diagnostics` on failure. Do not discard a Result; explicitly ignored results need a reason.

For a recoverable resource-loading exception:

```squirrel
try {
    level <- map.newLayerFromFile("maps/level.json");
} catch (error) {
    print("load level failed: " + error + "\n");
    level <- map.newLayer(20, 12, 32, 32);
}
```

## Ownership and lifecycle

- Keep worlds, textures, sources, emitters and scene hosts in persistent game state or entity components for as long as they are used.
- Create and load resources during initialization. Do not repeatedly load from disk in `eve_render()`.
- `update(dt)` normally takes seconds. Check the module's coordinate contract: 2D physics commonly uses pixel coordinates, while 3D physics uses meters.
- Consume input and network events during update; submit drawing during render.
- Top-level code runs again on hot reload. Use `persist` for state that must survive and migrate changed data layouts explicitly in `eve_reload()`.
