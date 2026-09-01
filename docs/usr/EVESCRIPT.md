# EveScript 完整教程

EveScript 是 EVEngine 的游戏脚本语言。它以 Squirrel 为基础，保留 Squirrel 的对象模型、闭包、
协程和动态类型能力，同时增加脚本模块、渐进类型、空值运算、命名参数、Inspector 元数据、热重载
持久变量、单位字面量、模式匹配以及 `async` / `await`。

所有游戏脚本仍使用 `.nut` 扩展名。项目不需要语言标记，也没有“启用 EveScript”的开关：
`config.nut`、`main.nut`、导入的脚本、REPL、调试器和 MCP 执行的代码都使用同一套语法。
没有使用扩展语法的标准 Squirrel 文件可以直接运行。

本教程面向使用 SDK 制作游戏的开发者。模块 API（图形、物理、UI、ECS 等）请配合
[模块使用手册](MODULES.md)查阅。

## 1. 第一个 EveScript 游戏

一个游戏至少包含 `config.nut` 和 `main.nut`：

```text
my-game/
├── config.nut
├── main.nut
└── scripts/
    └── movement.nut
```

`config.nut` 是普通 Squirrel 配置脚本。模块需求是整个项目的属性，因此在这里统一声明：

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

- `modules` 中的模块必须存在，否则引擎会在执行 `main.nut` 前停止并报告缺失项；
- 写出 `modules` 或 `optionalModules` 后，引擎只构造名单里的槽位，再加上启动循环需要的
  `win` / `gfx` / `timer` / `platform_event` / `fs` / `hot`。未列出的模块不会实例化，
  `has_module()` 为 false；需要时可用 `ensure_module("audio")` 再加载；
- 原生类的方法绑定也是按需的：第一次读取 `eve.Graphics` / `eve.Window` 等名字时才
  注册该类的全部方法（以及它在同一次 `expose` 里挂上的嵌套类型，如 `WindowSettings`）。
  Squirrel 的 `in` 不会触发这次绑定，因此不要用 `"Graphics" in eve` 判断模块是否在构建里，
  请用 `eve.moduleList` / `ensure_module()`；
- `optionalModules` 在 SDK 里有则加载，没有则跳过，使用前仍要调用 `has_module()`；
- 名称是脚本中的 root slot，例如图形模块写 `gfx`，不是 CMake 名称 `graphics`；
- 这两个字段必须是字符串数组字面量，语言服务器和打包器不会执行任意配置代码来推断模块。
- 不写这两个字段的旧项目保持原来的行为：构造当前构建里的全部模块。

`scripts/movement.nut`：

```squirrel
export const PLAYER_SPEED = 180.0

export function wrap_x(x: float, width: float) -> float {
    return x > width ? -48.0 : x
}
```

`main.nut`：

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

运行：

```sh
eve run my-game
```

保存脚本后，`persist playerX` 会在热重载时保留当前位置；函数和常量则由新代码替换。

从旧项目批量迁移 root 持久状态时，可先预览再写入：

```sh
python scripts/migrate_evescript.py --check
python scripts/migrate_evescript.py --write
```

迁移器只改写可证明等价的 root-table 判断和 `persist(name, initializer)` 样板；普通 Squirrel、
动态 root-table 操作和兼容性 `dofile()` 不会被猜测式重写。

## 2. 仍然需要掌握的 Squirrel 基础

EveScript 是 Squirrel 的兼容扩展，不是另一套对象模型。以下写法仍是日常代码的主体。

### 2.1 源文件、注释和语句

源码采用 UTF-8。标识符、字符串、数字和运算符沿用 Squirrel；语言关键字区分大小写。支持行注释
与块注释：

```squirrel
// 行注释

/*
 * 块注释
 */
```

分号与标准 Squirrel 一样可以作为语句终止符。项目代码建议依靠换行和大括号，不混用多种风格；
同一行放多条语句时必须用分号分隔。

常用运算符仍包括算术 `+ - * / %`、比较 `== != < <= > >=`、逻辑 `&& || !`、三元表达式
`condition ? yes : no`、成员访问 `.` / `[]` 和赋值运算。EveScript 新增的 `?.`、`??`、`??=`
见第 5 节。

`import`、`export`、`async`、`await`、`persist` 和 `match` 在对应语法位置具有语言含义；`from`
和 `as` 只在 import 子句中按关键字解析，因此旧脚本仍可把它们用作普通标识符。类型名称和单位
名称也主要在类型/字面量上下文中解析。

### 2.2 值、局部变量和 root slot

```squirrel
local lives = 3
local speed = 120.0
local title = "Arena"
local enabled = true
local target = null

local spawn = { x = 20.0, y = 40.0 }
local enemies = ["slime", "bat"]
```

`local` 创建词法作用域局部变量。`<-` 在表中创建新 slot，`=` 修改已有 slot：

```squirrel
gameState <- { score = 0 } // 在 root table 创建 gameState
gameState.score = 10       // 修改已有字段
```

优先使用 `local`、模块内私有值和显式 `export`。只有引擎生命周期回调等约定入口才需要放在 root
table。不要用 `const` 表达运行时只读状态：Squirrel 的 `const` 是编译期常量声明，适合固定数值和
导出的常量；普通可变游戏状态使用变量。

### 2.3 函数、闭包和默认参数

```squirrel
function clamp_health(value, maximum = 100) {
    return value < 0 ? 0 : (value > maximum ? maximum : value)
}

local multiplier = 2
local scale = function(value) {
    return value * multiplier // 闭包捕获外层变量
}
```

### 2.4 表、数组和遍历

```squirrel
local inventory = {
    potion = 2
    key = 1
}

inventory.potion += 1
inventory["coin"] <- 20

foreach (name, count in inventory)
    print(name + ": " + count + "\n")

local actors = ["hero", "merchant"]
actors.append("guard")
foreach (index, actor in actors)
    print(index + ": " + actor + "\n")
```

### 2.5 类、继承和 `this`

```squirrel
class Actor {
    name = ""
    health = 100

    constructor(name) {
        this.name = name
    }

    function damage(amount) {
        health -= amount
    }
}

class Player extends Actor {
    coins = 0
}

local player = Player("Ada")
player.damage(10)
```

类字段的默认值属于 Squirrel 类语义。引擎原生对象也通过相同的成员调用形式使用，但可调用的接口
以模块手册和绑定契约为准，并非 C++ 类的所有 public 方法。

### 2.6 控制流、异常和 generator

```squirrel
if (player.health <= 0) {
    print("game over\n")
} else {
    player.damage(1)
}

for (local i = 0; i < 3; ++i) {
    // ...
}

try {
    load_save()
} catch (error) {
    print("load failed: " + error + "\n")
}

function dialogue_lines() {
    yield "Hello"
    yield "Welcome to EVEngine"
}
```

`yield` 和 Squirrel generator 仍然保留，适合由调用者主动推进的序列。需要等待 Promise、计时器或
主线程事件时，优先使用 EveScript 的 `async function`。

## 3. 脚本模块：`import` 与 `export`

新代码使用模块，而不是用 `dofile()` 把多个文件写入同一个全局表。

### 3.1 导出

模块 root scope 可以导出常量、函数和类：

```squirrel
local nextId = 1 // 模块私有

export const DEFAULT_HEALTH = 100

export class ActorId {
    value = 0
    constructor(value) { this.value = value }
}

export function allocate_actor_id() {
    return ActorId(nextId++)
}
```

`export` 只能用于模块顶层的 `const`、`function` 或 `class` 声明。局部变量不会泄漏给导入方。

### 3.2 导入

```squirrel
import { ActorId, allocate_actor_id } from "game:/scripts/ids.nut"
import { allocate_actor_id as allocate_id } from "./ids.nut"
import * as ids from "./ids.nut"

local first = allocate_actor_id()
local second = allocate_id()
local third = ids.allocate_actor_id()
```

模块说明符必须是字符串字面量：

- `game:/...` 指向游戏包中的脚本；
- `engine:/...` 指向 SDK 标准脚本库；
- `plugin:<id>/...` 指向插件公开的脚本模块；
- `./...` 和 `../...` 相对于当前模块解析，但不能越出所属根目录。

同一规范 URI 在一次 Runtime generation 中只实例化一次。多处导入不会重复执行顶层代码。
循环导入会在编译依赖图时被拒绝；把共享类型或常量提取到第三个无环模块即可。

模块 URI 与物理文件格式无关，所以同一条 import 在开发目录、`.eve` ZIP、Android APK、iOS
bundle 和 Web 预加载文件系统中保持一致。打包器会扫描依赖图；动态拼接路径不是合法 import。

### 3.3 与 `dofile` 的区别

`dofile(path)` 仍为旧 Squirrel 脚本保留，但它表达的是“读取并执行文件”，没有私有命名空间、
显式导出、单次实例化缓存或静态依赖图。新项目应使用 `import`；只有迁移旧脚本或确实需要传统
共享 root-table 副作用时才使用 `dofile`。

## 4. 渐进类型

类型标注是可选的。未标注代码继续使用 Squirrel 动态语义；标注不会把值包装成新的运行时对象。

```squirrel
local speed: float = 180.0
local name: string = "Ada"
local targets: Array<string> = []
local scores: Table<string, int> = {}

function damage(amount: int, critical: bool) -> int {
    return critical ? amount * 2 : amount
}
```

基础类型为 `null`、`bool`、`int`、`float`、`string` 和 `dynamic`。容器使用 `Array<T>`、
`Table<K, V>`；引擎绑定类使用文档中的脚本类名。

类型是“渐进式静态检查”，不是完整的运行时类型系统：

- 字面量与已知函数调用会在编译期检查；
- 原生方法依赖 SDK 生成的 Binding Contract 检查参数名、类型和单位；
- `dynamic` 或无法推断的旧代码仍可能在运行时失败；
- 类型标注在生成字节码前擦除，普通局部值仍是原始 Squirrel 值。

例如：

```squirrel
local count: int = "many" // 编译错误

function set_count(value: int) {}
set_count(1.5)             // 编译错误
```

## 5. Nullable 与空值运算

`T?` 表示值可以为 `null`：

```squirrel
local target: Actor? = find_target()
```

常用运算符：

```squirrel
target?.damage(10)                    // target 为 null 时不调用，结果为 null
local label = target?.name ?? "None" // 左侧为 null 才计算右侧
target ??= find_fallback_target()     // 仅在 target 为 null 时赋值
```

接收者和左操作数只求值一次，`??` 具有短路语义：

```squirrel
local target = cached ?? expensive_lookup()
```

当 `cached` 非空时，`expensive_lookup()` 不会执行。成员 slot 的 `??=` 也受支持：

```squirrel
local state = { checkpoint = null }
state.checkpoint ??= make_checkpoint()
```

非 nullable 类型不能直接赋 `null`。来自旧代码的动态值仍应在原生资源、场景查询等边界做好错误
处理。

## 6. 字符串取值类型

EVEngine 的模式、类型和策略名称在 C++ 边界大量使用字符串。EveScript 用字符串字面量联合约束
合法值，而不强制把它们改成整数枚举：

```squirrel
local movement: "idle" | "run" | "jump" = "idle"

function set_surface(mode: "opaque" | "mask" | "blend") {
    material.setSurfaceMode(mode)
}
```

好处是补全、拼写检查、`match` 穷尽检查和 Inspector 下拉选项都能共享同一组值；传入 C++、
JSON 或日志时仍是普通字符串。

```squirrel
set_surface("transparent") // 已知字面量不在集合中，编译错误
```

## 7. 命名参数

调用时用 `name: value` 指明参数：

```squirrel
local world = physics.newWorld(
    gravityY: 980.0,
    sleep: true,
    gravityX: 0.0
)
```

编译器根据脚本函数声明或原生 Binding Contract 还原成位置调用，因此不会创建参数表，也没有额外
逐帧分配。参数可以重排；未知、重复或缺失参数是编译错误。

```squirrel
function blend(from: float, to: float, weight: float) -> float {
    return from + (to - from) * weight
}

local value = blend(weight: 0.25, from: 0.0, to: 1.0)
```

参数表达式仍按源码出现顺序求值，而不是按声明顺序求值。不要依赖重排后的隐藏副作用：

```squirrel
compose(third: log_and_return(3), first: log_and_return(1), second: log_and_return(2))
// 调用日志顺序仍是 3、1、2；传给函数的槽位顺序则是 first、second、third。
```

无法取得可靠 Binding Contract 的动态原生函数不能使用命名参数，改用位置参数。

SDK 构建会从当前 profile 中实际参与编译的 SimpleSquirrel `addFunc` 声明生成契约。无法解析真实
参数名的绑定会直接使生成步骤失败，不会退化成 `arg0`、`arg1`。生成结果在 `ScriptCompiler`
创建时注册到 SimpleSquirrel 的 named-argument resolver，因此编译检查、补全、Hover 与运行时
使用的是同一份契约。C++ 默认实参不会自动成为脚本默认值；不同类上同名且签名有歧义的方法应
继续使用位置参数。契约类型、字符串选项和单位检查只约束参数表达式边界上的直接字面量；合法的
复合表达式（例如 `ui.setText("status", "count " + (count + 1))`）会按正常表达式语义编译，
不会把内部数值字面量误判成字符串参数。

## 8. Inspector 属性注解

注解是 Squirrel attribute 的短写，放在类字段之前：

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

它等价于已有的 Squirrel attribute 元数据，Inspector、反射和 MCP 读取的仍是同一份信息。字符串
取值类型配合 `@editor("combo")` 时会自动生成选项。

内置注解拼写错误会在编译期报告。插件注解必须先由插件注册，例如插件可能提供
`@plugin_asset("texture")`；未注册的未知注解不能静默忽略。

## 9. 热重载持久变量：`persist`

普通变量在脚本重载后没有保留承诺。需要保留的模块/root 状态显式声明为 `persist`：

```squirrel
persist score: int = 0
persist session: Table<string, dynamic> = {}
```

初始化表达式只在该名称第一次出现时执行：

```squirrel
persist world = create_world() // 后续热重载不会再次调用 create_world()
```

规范：

- `persist` 只允许在文件 root scope；
- 名称在 root table 中必须唯一；
- 未写 `persist` 的变量就是普通临时脚本状态，不需要 `transient`；
- 优先保存普通数据；无法安全快照的原生句柄应由对应引擎模块的状态提供器管理；
- 修改持久数据结构时，要在 `eve_reload()` 中做显式版本迁移或回退。

旧式写法仍兼容：

```squirrel
score <- persist("score", function() { return 0 })
```

新代码使用声明形式，避免名称字符串与变量名不一致。

## 10. 单位字面量

时间、角度和空间量可以直接携带单位：

```squirrel
local fade: seconds = 250ms   // 0.25
local turn: radians = 90deg   // π / 2
local tileSize: pixels = 32px // 32.0
local height: meters = 1.8m   // 1.8
```

v1 支持的量纲和后缀：

| 量纲 | 目标类型 | 字面量后缀 | 说明 |
|---|---|---|---|
| 时间 | `seconds`、`milliseconds` | `ms` | `250ms` 转为 `seconds` 时是 `0.25` |
| 角度 | `radians`、`degrees` | `deg` | `90deg` 转为 `radians` 时约为 `1.5708` |
| 像素 | `pixels` | `px` | 保持浮点标量 |
| 距离 | `meters` | `m` | 保持浮点标量 |

原生 Binding Contract 声明目标单位后，调用处会自动进行确定的换算：

```squirrel
camera.setYaw(90deg) // 若参数契约为 radians，编译为弧度标量
asyncSleep(250ms)    // asyncSleep 接收毫秒，数值为 250.0
```

没有目标类型或绑定契约时，字面量采用自身的基础单位数值；不同量纲不能混传。单位只参与编译期
检查与常量换算，运行时仍是普通 float。

## 11. `match` 模式匹配

v1 的 `match` 是语句，不是返回值的表达式：

```squirrel
function update_animation(mode: "idle" | "run" | "jump") {
    match mode {
        "idle" => animation.play("idle")
        "run" => animation.play("run")
        "jump" => animation.play("jump")
    }
}
```

分支可以是单条语句或代码块：

```squirrel
match eventName {
    "damage" => {
        flash_red()
        play_hit_sound()
    }
    "heal" => show_heal_effect()
    else => print("unknown event: " + eventName + "\n")
}
```

规则：

- 字符串取值联合的所有选项都覆盖时，可以省略 `else`；
- 未穷尽联合类型必须提供剩余分支，否则编译错误；
- 动态值无法静态证明穷尽，因此必须有 `else`；
- `else` 只能出现一次且必须是最后一个分支；
- 被匹配的表达式只求值一次。

需要产生值时，在各分支中赋给外部局部变量或直接 `return`；不要写成表达式赋值。

## 12. 异步编程：`async function` 与 `await`

EveScript 异步建立在 EVEngine 现有 Promise、计时器、事件队列和主线程 pump 上：

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

语义：

- 调用 `async function` 立即返回 Promise；
- `await value` 会用 `Promise.resolve(value)` 统一普通值和 Promise；
- Promise 完成后，continuation 在游戏线程的异步队列恢复；
- `try` / `catch` 可以捕获被拒绝 Promise 的原因；
- `return value` 完成外层 Promise，未捕获异常会拒绝它；
- `await` 只能出现在 `async function` 内；
- 异步函数不会把 Squirrel VM 移到 worker 线程。

循环、局部变量、`this` 和异常状态会跨 `await` 保存：

```squirrel
class Counter {
    offset = 4

    async function compute(limit: int) -> int {
        local total = this.offset
        for (local i = 0; i < limit; ++i)
            total += await Promise.resolve(i)

        try {
            await Promise.reject("expected")
        } catch (error) {
            total += 10
        }
        return total
    }
}
```

引擎帧循环会自动调用 `async_pump()`，游戏代码通常不应手动 pump。脚本热重载会取消旧 generation
尚未恢复的 continuation，防止旧闭包在新代码和新状态上继续运行；需要长期可恢复的任务时，应把
进度保存为普通数据，并在重载后重新启动任务。

Promise 不等于后台线程。CPU 密集任务应使用 thread 模块提供的受支持 worker 操作，再通过事件、
Channel 或 Promise 回到主线程；worker 不得访问 Squirrel VM、窗口、GPU 或脚本对象。

当文件监听器发现已导入模块变化时，Runtime 会重编译该模块及反向依赖它的全部缓存 generation，
并在整组实例化成功后一次性提交。任一依赖编译或实例化失败都会恢复旧模块和旧依赖图；未进入
模块图的兼容性 root 脚本仍沿用软重载路径。

## 13. 与响应式事件配合

`async` / `await` 适合有明确开始、等待和结束的流程；事件或 Rx 适合持续数据流。不要把所有异步
逻辑都写成永久等待的任务：

- 一次加载、延迟、过场流程：`async function`；
- 多个监听者的离散游戏事件：event 模块；
- 输入、状态或事件的持续组合与过滤：rx 模块；
- 跨线程工作：thread + Channel/Event，最终回到主线程。

具体接口见[事件模块](modules/event.md)、[响应式编程模块](modules/rx.md)和
[线程与异步模块](modules/thread.md)。

## 14. 生命周期、模块与推荐组织方式

生命周期回调仍使用引擎约定的 root slot：

```squirrel
eve_init <- function() {}
eve_update <- function(dt) {}
eve_render <- function() {}
eve_reload <- function() {}
eve_asset_reload <- function(path) {}
eve_quit <- function() {}
```

推荐把 `main.nut` 保持为编排层：导入系统、创建顶层持久状态、连接生命周期。具体玩法逻辑放进
各自模块：

```text
scripts/
├── player.nut
├── combat.nut
├── ui/
│   └── hud.nut
└── world/
    └── dungeon.nut
```

模块建议：

- export 少量稳定的类和函数，不导出可随意修改的内部表；
- 避免模块顶层产生不可重复的外部副作用，把初始化放进显式函数；
- 模块间保持有向无环依赖；共享定义放到更底层的小模块；
- 需要跨重载的数据用 `persist`，可重建缓存则保持普通变量；
- 必需引擎模块写进 `config.modules`，可选功能同时使用 `optionalModules` 和 `has_module()`。

可选模块示例：

```squirrel
if (has_module("audio")) {
    // audio 是当前构建注入的全局 Audio 实例；Source 由它创建并播放。
    audio.stopAll()
}
```

## 15. 错误处理和编码规范

推荐规范：

1. 文件名使用小写 `snake_case.nut`，模块 URI 使用 `/`；
2. 局部变量和函数使用 `snake_case`，类使用 `PascalCase`，导出常量使用 `UPPER_SNAKE_CASE`；
3. 默认使用 `local`，跨文件接口使用 `export`，不要把普通状态随意写入 root table；
4. 公共函数、引擎边界和持久状态优先写类型；短小内部动态代码可以不标注；
5. 字符串模式使用联合类型，不为方便而引入脚本整数枚举；
6. 多个相邻数字或布尔参数优先使用命名参数；
7. 时间、角度和空间参数优先携带单位；
8. 对可恢复的资源错误使用 `try` / `catch`，不要吞掉编程错误；
9. 每帧热路径避免临时大数组、动态 import 和不必要的跨 C++/Squirrel 细粒度调用；
10. 异步任务必须考虑失败、退出和热重载取消。

原生绑定遇到非法字符串选项、参数类型错误、空对象或资源不存在时通常会抛 Squirrel 异常。对可
恢复错误：

```squirrel
try {
    load_level("levels/forest.json")
} catch (error) {
    print("cannot load forest: " + error + "\n")
    load_level("levels/fallback.json")
}
```

## 16. EveScript 与标准 Squirrel 的区别

| 能力 | 标准 Squirrel | EveScript |
|---|---|---|
| 文件 | `.nut` | 仍为 `.nut`，全项目默认启用 |
| 模块 | 通常用 `dofile` 和共享 root table | `import` / `export`、私有环境、缓存和静态依赖图 |
| 类型 | 动态类型 | 可选渐进类型，编译后擦除 |
| 空值 | 手写 `x == null` | `T?`、`?.`、`??`、`??=` |
| 有限选项 | 字符串或整数常量 | 字符串字面量联合，运行时仍是字符串 |
| 调用参数 | 位置参数 | 已知签名支持 `name: value` |
| 字段元数据 | `</ ... />` attribute | `@editor(...)`、`@unit(...)` 短写 |
| 热重载状态 | 手写 root 判断或 `persist(name, fn)` | `persist name = initializer` 声明 |
| 单位 | 裸数字 | `250ms`、`90deg`、`32px`、`1.8m` |
| 分支 | `if` / `switch` | 额外提供可穷尽检查的 `match` 语句 |
| 异步 | generator、thread、Promise 链 | 额外提供 Promise 状态机 `async` / `await` |
| VM 与对象模型 | Squirrel VM | 不变，继续使用同一 VM 和原生绑定 |

EveScript 没有增加以下概念：

- 没有文件级语言版本标记；
- 没有默认不可变变量，也没有 `let` / `var` 分裂；
- 没有要求所有变量写类型；
- 没有把普通变量标成 `transient`；未写 `persist` 就是普通变量；
- 没有隐藏 ECS 的新 DSL，ECS 继续通过 `eve.Component`、`eve.Entity`、`eve.System` 和模块 API；
- 没有统一的 Signal 关键字，事件、Rx、Promise 和状态绑定各自承担适合的职责；
- 没有独立虚拟机或 TypeScript 式外部转译运行时。

## 17. 从现有 Squirrel 项目迁移

迁移可以逐文件进行，因为标准 Squirrel 是 EveScript 的兼容子集。

### 第一步：直接运行并建立基线

不要先机械重写。升级 SDK 后先运行现有游戏，修复真实的不兼容或绑定变化。原有类、闭包、
generator、table、array、attribute 和 `dofile` 都仍然可用。

### 第二步：声明项目模块

把所有脚本无条件使用的模块写入 `config.modules`；有降级路径的模块写入
`config.optionalModules` 并保留 `has_module()`。写出任一场后，未点名的模块不再在启动时构造。

### 第三步：用 import/export 替换文件级共享副作用

旧代码：

```squirrel
// player.nut
Player <- class {}

// main.nut
dofile("scripts/player.nut")
local player = Player()
```

新代码：

```squirrel
// scripts/player.nut
export class Player {}

// main.nut
import { Player } from "./scripts/player.nut"
local player = Player()
```

### 第四步：只给稳定边界补类型

先标注导出函数、持久状态、Inspector 字段和原生 API 边界，不必一次给所有局部变量加类型。

### 第五步：替换重复样板

- root-table 热重载判断改成 `persist`；
- 判空链改成 `?.` / `??`；
- 多个同类型参数改成命名参数；
- 毫秒、角度和距离裸数改成单位字面量；
- Promise 链较深且有顺序流程时改成 `async` / `await`。

每一步都保持游戏可运行，并单独验证热重载和打包结果。

## 18. 工具、诊断和调试

桌面 SDK 提供 EveScript Language Server：

```sh
eve language-server --root /path/to/my-game
```

它通过标准输入输出使用 LSP 协议，供编辑器插件启动；不要在同一 stdio 通道输出额外日志。当前
能力包括：

- 语法和结构化诊断；
- `config.modules` / `optionalModules` 检查；
- 已知脚本符号与 Binding Contract 补全；
- 原生方法签名和文档 Hover；
- 跨文件 `import` / `export` 定义跳转；
- 项目级引用查找；
- 导出符号和 import alias 的安全重命名；
- 文档打开、增量更新和关闭同步。

运行时调试：

```sh
eve run --debug --dap-port=4711 my-game
```

EveScript 编译器为扩展语法生成 Source Map，断点、停止位置、调用栈和异常会映射回原始 `.nut`
源码，而不是显示内部状态机位置。打包前可运行：

```sh
eve zip my-game
```

打包器会验证脚本模块图，缺失模块、循环依赖或越界 URI 会在生成归档前失败，并在包内写入脚本
模块清单。

常见诊断处理：

- `await is only allowed inside async function`：给所在函数加 `async`，或改用 Promise 链；
- `named arguments require a known function or Binding Contract`：改用位置参数，或确认 SDK 绑定契约；
- `non-exhaustive match`：补齐字符串联合选项，或添加最终 `else`；
- `outside the allowed choices`：使用联合类型或绑定契约列出的字符串；
- `persist is only allowed at root scope`：把声明移到模块顶层；
- `required module is missing`：更换包含该模块的 SDK/profile，或移入 `optionalModules` 并实现降级；
- 模块循环：提取共享定义，保持依赖图有向无环。

## 19. 完整示例

下面把模块、类型、nullable、持久状态、单位、match、命名参数和异步流程组合在一起。

`scripts/player.nut`：

```squirrel
export class PlayerState {
    @editor("slider", min: 0, max: 100, step: 1)
    health: int = 100

    @editor("combo")
    mode: "idle" | "run" | "hurt" = "idle"

    x: float = 100.0
}

export function update_player(
    player: PlayerState,
    dt: float,
    speed: float
) {
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

`main.nut`：

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
    // 文本绘制需要先通过 font 模块创建字体；这里用色条表示异步提示仍在显示。
    if (notice != null)
        gfx.drawSolidRect(32.0, 32.0, 180.0, 8.0, 1.0, 0.8, 0.2, 1.0)
}
```

实际项目中的绘制和输入函数请以当前 SDK 的[模块使用手册](MODULES.md)为准。学习顺序建议是：
先掌握 Squirrel 的 table、array、class 和闭包，再使用模块与类型建立代码边界，最后在确有等待流程
时引入 `async` / `await`。
