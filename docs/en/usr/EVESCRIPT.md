# EveScript Tutorial

[中文](../../usr/EVESCRIPT.md) · [User guide](README.md) · [Module handbook](MODULES.md)

This tutorial is maintained by hand; the Chinese documentation is authoritative.

EveScript is EVEngine's game scripting language. It extends Squirrel with script modules, gradual types, null operators, named arguments, Inspector annotations, persistent hot-reload state, unit literals, pattern matching and `async` / `await`. It keeps Squirrel's VM, object model, closures, generators and dynamic values.

All scripts use `.nut`. No language marker or feature switch is required: configuration, game files, imports, REPL and debugger code use the same syntax. Ordinary Squirrel remains supported.

## 1. Your first game

Create `config.nut`, `main.nut` and `scripts/movement.nut` in your game directory.

`config.nut`:

```squirrel
config <- {
    width = 960
    height = 540
    title = "EveScript Tutorial"
    hotReload = true
    modules = ["gfx", "keyboard", "timer"]
    optionalModules = ["audio"]
}
```

- Required `modules` must exist; otherwise startup fails before `main.nut` executes.
- Once either module field is present, only listed slots and the required startup slots (`win`, `gfx`, `timer`, `platform_event`, `fs`, `hot`) are constructed. Unlisted slots return false from `has_module()`; use `ensure_module("audio")` when needed.
- Optional modules load when available. Check `has_module()` before using them.
- Names are script root slots: `gfx`, not the CMake name `graphics`.
- Both fields must be literal string arrays so tools can inspect them without executing configuration code.
- Projects omitting both fields keep the previous behavior of constructing all compiled modules.
- Native classes bind lazily on first access to names such as `eve.Graphics`. The Squirrel `in` operator does not trigger binding. Do not test `"Graphics" in eve`; use `eve.moduleList`, `has_module()` or `ensure_module()`.

`scripts/movement.nut`:

```squirrel
export const PLAYER_SPEED = 180.0

export function wrap_x(x: float, width: float) -> float {
    return x > width ? -48.0 : x
}
```

`main.nut`:

```squirrel
import { PLAYER_SPEED, wrap_x } from "./scripts/movement.nut"

persist playerX: float = 100.0

eve_init <- function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0)
}

eve_update <- function(dt: float) {
    playerX = wrap_x(playerX + PLAYER_SPEED * dt, config.width)
}

eve_render <- function() {
    gfx.clear()
    gfx.drawSolidRect(playerX, 220.0, 48.0, 48.0, 0.3, 0.75, 1.0, 1.0)
}
```

Run `eve run my-game`. After saving a changed script, `persist playerX` retains its value while functions and constants are replaced.

The source repository also provides `python scripts/migrate_evescript.py --check` to preview migration and `--write` to apply it. It rewrites only provably equivalent root-state guards and `persist(name, initializer)` patterns; it does not guess how to rewrite dynamic root operations or `dofile()`.

## 2. Squirrel foundations

Source files use UTF-8. Keywords are case-sensitive. Use `//` and `/* ... */` comments. Semicolons remain valid; use them to separate multiple statements on one line. Prefer a consistent style relying on newlines and braces.

Arithmetic, comparison, logical operators, ternary expressions, member access and assignment retain their Squirrel meanings. `import`, `export`, `async`, `await`, `persist` and `match` have special meanings in their syntax positions. `from` and `as` are contextual import keywords, so legacy identifiers can still use them elsewhere.

```squirrel
local lives = 3
local speed = 120.0
local spawn = { x = 20.0, y = 40.0 }
local enemies = ["slime", "bat"]

gameState <- { score = 0 } // Create a root-table slot.
gameState.score = 10       // Assign an existing slot.
```

`local` creates a lexically scoped variable. `<-` creates a slot; `=` changes an existing slot. Prefer local/private state and explicit exports. Reserve root slots for lifecycle callbacks and other intentional shared entry points. Squirrel `const` is a compile-time constant, not a general runtime read-only variable.

Functions support defaults and closures:

```squirrel
function clamp_health(value, maximum = 100) {
    return value < 0 ? 0 : (value > maximum ? maximum : value)
}
local multiplier = 2
local scale = function(value) { return value * multiplier }
```

Tables, arrays and iteration:

```squirrel
local inventory = { potion = 2, key = 1 }
inventory.potion += 1
inventory["coin"] <- 20
foreach (name, count in inventory)
    print(name + ": " + count + "\n")

local actors = ["hero", "merchant"]
actors.append("guard")
foreach (index, actor in actors)
    print(index + ": " + actor + "\n")
```

Classes, inheritance and `this`:

```squirrel
class Actor {
    name = ""
    health = 100
    constructor(name) { this.name = name }
    function damage(amount) { health -= amount }
}
class Player extends Actor { coins = 0 }
local player = Player("Ada")
player.damage(10)
```

Native objects use the same member-call syntax, but their callable methods come from script bindings, not every C++ public declaration.

`if`, `for`, `try` / `catch` and Squirrel generators still work:

```squirrel
try {
    load_save() // Your game's loader.
} catch (error) {
    print("load failed: " + error + "\n")
}
function dialogue_lines() {
    yield "Hello"
    yield "Welcome to EVEngine"
}
```

Use generators for sequences advanced by their caller. Use `async function` when waiting for promises, timers or main-thread events.

## 3. Script modules: import and export

New code should use explicit modules instead of sharing a root table through `dofile()`.

```squirrel
// scripts/ids.nut
local nextId = 1 // Private to this module.
export const DEFAULT_HEALTH = 100
export class ActorId {
    value = 0
    constructor(value) { this.value = value }
}
export function allocate_actor_id() { return ActorId(nextId++) }
```

`export` applies only to top-level `const`, `function` and `class` declarations. Private locals do not leak to importers.

```squirrel
import { ActorId, allocate_actor_id } from "game:/scripts/ids.nut"
import { allocate_actor_id as allocate_id } from "./ids.nut"
import * as ids from "./ids.nut"
```

Module specifiers must be string literals:

- `game:/...`: game-package scripts.
- `engine:/...`: SDK standard scripts.
- `plugin:<id>/...`: a plugin's exported scripts.
- `./...` and `../...`: relative to the importing module, without escaping its root.

A canonical URI is instantiated once per runtime generation. Cyclic imports are rejected; move shared definitions to a third module. URIs work across development directories, `.eve` archives, APKs, iOS bundles and Web preloaded filesystems. Dynamically assembled import paths are not supported.

`dofile(path)` remains available for legacy execution and intentional shared root-table side effects. It does not provide module privacy, explicit exports, single-instantiation caching or a static dependency graph.

## 4. Gradual types

Annotations are optional and erased before bytecode generation. Values remain ordinary Squirrel values.

```squirrel
local speed: float = 180.0
local name: string = "Ada"
local targets: Array<string> = []
local scores: Table<string, int> = {}

function damage(amount: int, critical: bool) -> int {
    return critical ? amount * 2 : amount
}
```

Primitive types are `null`, `bool`, `int`, `float`, `string` and `dynamic`. Use `Array<T>` and `Table<K, V>` for containers, and documented script class names for native types.

Known literals and function calls are checked at compile time. Native calls use the SDK's Binding Contracts for parameter names, types and units. `dynamic` and uninferred legacy code can still fail at runtime. For example, assigning `"many"` to an `int`, or passing `1.5` to a known `int` parameter, is a compile error.

## 5. Nullable values

`T?` permits `null`:

```squirrel
local target: Actor? = find_target()
target?.damage(10)
local label = target?.name ?? "None"
target ??= find_fallback_target()

local state = { checkpoint = null }
state.checkpoint ??= make_checkpoint()
```

The helper functions above are supplied by your game. `?.` skips access/calls on null receivers; `??` evaluates its right side only when the left side is null; `??=` assigns only when null. Receivers and left operands are evaluated once. Non-nullable types cannot be assigned null. Validate dynamic legacy values at native/resource boundaries.

## 6. String choice types

Use string literal unions for mode and policy names:

```squirrel
local movement: "idle" | "run" | "jump" = "idle"
function set_surface(mode: "opaque" | "mask" | "blend") {
    material.setSurfaceMode(mode)
}
```

Completion, spelling checks, exhaustive matching and Inspector choices use the same values. Runtime values remain strings. Passing a known literal outside the union is a compile error. `material` here is an existing game resource, not a new language construct.

## 7. Named arguments

```squirrel
local world = physics.newWorld(
    gravityY: 980.0,
    sleep: true,
    gravityX: 0.0
)
function blend(from: float, to: float, weight: float) -> float {
    return from + (to - from) * weight
}
local value = blend(weight: 0.25, from: 0.0, to: 1.0)
```

The compiler uses a known script signature or native Binding Contract to lower the call to positional arguments. No argument table or extra per-frame allocation is created. Unknown, duplicate or missing arguments are compile errors.

Expressions are evaluated in their written order, even if parameter order differs. Dynamic native functions without a reliable contract require positional arguments. C++ default arguments do not automatically become script defaults; ambiguous methods also need positional calls.

SDK contract generation reads actual `addFunc` bindings from the build's profile. It fails on unresolvable parameter names instead of inventing `arg0` names. The compiler, editor and runtime share the contracts. Literal checks apply at argument-expression boundaries; they do not misclassify numbers inside a valid compound expression such as `"count " + (count + 1)`.

## 8. Inspector annotations

Annotations are shorthand for Squirrel attribute metadata:

```squirrel
class CharacterData {
    @editor("slider", min: 0, max: 100, step: 1)
    @unit("hp")
    health: int = 100

    @editor("combo")
    job: "warrior" | "mage" | "rogue" = "warrior"

    @editor("text")
    displayName: string = "Ada"
}
```

Inspector, reflection and MCP read the same metadata. String choice types supply combo options. Misspelled built-in annotations are compile errors. Plugin annotations must be registered first; unknown annotations are not silently ignored.

## 9. Persistent hot-reload variables

Ordinary variables have no retention guarantee across reloads. Declare persistent root state explicitly:

```squirrel
persist score: int = 0
persist session: Table<string, dynamic> = {}
persist world = create_world() // Only the first declaration runs the initializer.
```

- `persist` is valid only at file root scope.
- Names must be unique in the root table.
- Ordinary variables need no `transient` marker.
- Prefer plain data; native handles that cannot be safely snapshotted belong to their modules' state providers.
- Migrate changed persistent layouts explicitly in `eve_reload()`.

Legacy `score <- persist("score", function() { return 0 })` remains compatible. Prefer the declaration syntax in new code so variable and string names cannot diverge.

## 10. Unit literals

```squirrel
local fade: seconds = 250ms   // 0.25
local turn: radians = 90deg   // pi / 2
local tileSize: pixels = 32px // 32.0
local height: meters = 1.8m   // 1.8
```

| Dimension | Target types | Suffix |
| --- | --- | --- |
| Time | `seconds`, `milliseconds` | `ms` |
| Angle | `radians`, `degrees` | `deg` |
| Pixels | `pixels` | `px` |
| Distance | `meters` | `m` |

A known target type or native contract performs deterministic conversion. `camera.setYaw(90deg)` receives radians if its contract specifies radians; `asyncSleep(250ms)` receives milliseconds. Without a target, the literal uses its own base-unit value. Dimensions cannot be mixed. Units are compile-time checks/conversions; runtime values remain floats.

## 11. Pattern matching

`match` is a statement, not a value-producing expression:

```squirrel
function update_animation(mode: "idle" | "run" | "jump") {
    match mode {
        "idle" => animation.play("idle")
        "run" => animation.play("run")
        "jump" => animation.play("jump")
    }
}
```

Branches accept a statement or a block. Cover every string-union choice or provide a final `else`. Dynamic values always need `else`, because coverage cannot be proven. `else` may occur once and must be last. The matched expression is evaluated once. To produce a value, assign an outer local or return from branches.

## 12. Async functions and await

EveScript async uses engine promises, timers, events and the main-thread pump:

```squirrel
async function intro_sequence() -> string {
    await asyncSleep(250ms)
    ui.setText("message", "Ready")
    local values = await Promise.all([
        load_profile_async(),
        load_inventory_async()
    ])
    return values[0].name
}
intro_sequence().then(
    function(name) { print("welcome " + name + "\n") },
    function(error) { print("intro failed: " + error + "\n") }
)
```

The loaders are game-defined. Calling an async function immediately returns a Promise. `await` normalizes values through `Promise.resolve`. Continuations resume on the game thread. `try` / `catch` handles rejection; returning resolves the outer Promise, and uncaught exceptions reject it. `await` is valid only inside async functions.

Loops, locals, `this` and exception state survive suspension. The engine calls `async_pump()` each frame; game code normally should not pump manually. Hot reload cancels pending continuations from the old generation. Save progress as plain data and restart long-lived work after reload.

Async does not move the Squirrel VM onto workers. CPU-heavy tasks use supported thread-module operations and return through Channel/Event/Promise. Workers must not access VM, window, GPU or script objects.

When an imported module changes, the runtime recompiles it and reverse dependencies and commits the group only after successful instantiation. A failure restores the previous modules and dependency graph. Legacy root scripts outside the module graph use soft reload.

## 13. Async, events and reactive streams

Choose by the task:

- One-time loads, delays and cutscenes: async functions.
- Discrete gameplay events with multiple listeners: event module.
- Continuous filtering/composition of input and state: Rx.
- Worker execution: thread plus Channel/Event, returning to the main thread.

See the Chinese [event](../../usr/modules/event.md), [Rx](../../usr/modules/rx.md) and [thread](../../usr/modules/thread.md) guides. Do not turn every event stream into an indefinitely waiting async task.

## 14. Organize lifecycle and modules

Lifecycle callbacks remain engine-defined root slots:

```squirrel
eve_init <- function() {}
eve_update <- function(dt) {}
eve_render <- function() {}
eve_reload <- function() {}
eve_asset_reload <- function(path) {}
eve_quit <- function() {}
```

Keep `main.nut` as orchestration: import systems, create top-level persistent state and wire callbacks. Put player, combat, HUD and world logic in their own modules.

Export a small stable interface. Avoid non-repeatable top-level side effects; initialize explicitly. Keep dependencies acyclic. Persist durable data while leaving rebuildable caches temporary. Declare required modules in `config.modules`; optional features use both `optionalModules` and `has_module()`.

```squirrel
if (has_module("audio")) audio.stopAll()
```

## 15. Error handling and coding style

Use lowercase `snake_case.nut` filenames and `/` in module URIs. Use `snake_case` for locals/functions, `PascalCase` for classes and `UPPER_SNAKE_CASE` for exported constants. Prefer locals and explicit exports over root-table writes.

Annotate public functions, engine boundaries and persistent state first. Use string unions for choices, named arguments for similar adjacent parameters, and unit literals for time/angles/distances.

Catch recoverable resource errors; do not hide programming mistakes. Avoid large temporary arrays, dynamic imports and unnecessary fine-grained C++/Squirrel calls in per-frame paths. Async tasks must account for failure, exit and reload cancellation. See [API conventions](API-CONVENTIONS.md) for Result handling.

## 16. Differences from ordinary Squirrel

EveScript adds private import/export modules, erased gradual types, nullable operators, string unions, named arguments, short attribute annotations, persistent declarations, units, exhaustive `match` and Promise-based async syntax.

It retains `.nut`, the same VM and native bindings. It does not require types everywhere, introduce `let` / `var`, mark ordinary variables as transient, add an ECS DSL or create a second runtime. Events, Rx, promises and state bindings keep their separate roles.

## 17. Migrate an existing project

1. Upgrade the SDK and run the existing game first. Ordinary classes, closures, generators, tables, arrays, attributes and `dofile()` remain supported.
2. Declare required and optional project modules; listing either field stops unlisted modules from being constructed at startup.
3. Replace shared file-level side effects with imports and exports.
4. Add types to stable boundaries rather than every local at once.
5. Replace repeated root guards with `persist`, null checks with null operators, ambiguous positional calls with named arguments, bare unit values with unit literals, and deeply nested sequential Promise chains with async functions.

Keep the game runnable after each step and verify hot reload and packaging separately.

## 18. Tools, diagnostics and debugging

```sh
eve language-server --root /path/to/my-game
eve run --debug --dap-port=4711 my-game
eve zip my-game
```

The language server uses standard input/output for LSP; do not mix extra logs into that channel. The repository's `tools/vscode-eve-debug/` starts it for `.nut` files in VS Code/Cursor. Features include syntax/module diagnostics, binding completion and hover, signature help, outlines, cross-file definition/reference lookup, safe export/alias renaming, formatting, folding, semantic highlighting and incremental document updates.

Source maps keep breakpoints, stacks and exceptions tied to your original `.nut` files rather than lowered state-machine code. Packaging validates the static module graph, rejects missing modules, cycles and escaping URIs, and writes a module manifest.

Common diagnostics:

- `await is only allowed inside async function`: make the function async or use a Promise chain.
- `named arguments require a known function or Binding Contract`: use positional arguments or check your SDK contract.
- `non-exhaustive match`: cover remaining choices or add a final `else`.
- `outside the allowed choices`: use a documented string choice.
- `persist is only allowed at root scope`: move it to module top level.
- `required module is missing`: use a matching SDK/profile, or make the feature optional with a fallback.
- Import cycle: extract shared definitions into an acyclic dependency.

## 19. A complete example

Use the configuration from section 1. `scripts/player.nut`:

```squirrel
export class PlayerState {
    @editor("slider", min: 0, max: 100, step: 1)
    health: int = 100
    @editor("combo")
    mode: "idle" | "run" | "hurt" = "idle"
    x: float = 100.0
}
export function update_player(player: PlayerState, dt: float, speed: float) {
    if (keyboard.isDown("right")) {
        player.x += speed * dt
        player.mode = "run"
    } else {
        player.mode = "idle"
    }
}
export function render_player(player: PlayerState) {
    match player.mode {
        "idle" => gfx.drawSolidRect(player.x, 220.0, 48.0, 48.0, 0.3, 0.75, 1.0, 1.0)
        "run" => gfx.drawSolidRect(player.x, 220.0, 48.0, 48.0, 0.2, 1.0, 0.5, 1.0)
        "hurt" => gfx.drawSolidRect(player.x, 220.0, 48.0, 48.0, 1.0, 0.2, 0.2, 1.0)
    }
}
```

`main.nut`:

```squirrel
import { PlayerState, update_player, render_player } from "./scripts/player.nut"
persist player: PlayerState = PlayerState()
local notice: string? = null

async function show_ready_message() {
    notice = "Get ready"
    await asyncSleep(750ms)
    notice = "Go!"
    await asyncSleep(500ms)
    notice = null
}
eve_init <- function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0)
    show_ready_message().then(
        function(_) {},
        function(error) { print("message sequence failed: " + error + "\n") }
    )
}
eve_update <- function(dt: float) {
    update_player(player: player, speed: 180.0, dt: dt)
}
eve_render <- function() {
    gfx.clear()
    render_player(player)
    // A bar shows whether the async notice is still active.
    if (notice != null)
        gfx.drawSolidRect(32.0, 32.0, 180.0, 8.0, 1.0, 0.8, 0.2, 1.0)
}
```

Learn Squirrel tables, arrays, classes and closures first; use modules and types to establish boundaries; add async when a workflow actually waits. Consult your SDK's API catalog for exact rendering and input signatures.
