# EveScript 语言设计

> 状态：Design Draft v1
> 日期：2026-08-25
> 运行后端：EVEngine 当前 Squirrel VM

## 1. 定位

EveScript 是 EVEngine 唯一的脚本语言入口，也是 Squirrel 的源码兼容扩展。它不改变虚拟机、
对象模型、原生模块实例、ECS、Scene 或帧循环，而是在编译阶段提供适合游戏开发的静态检查、
模块系统和语法糖。

项目内所有 `.nut` 文件都经过 EveScript 编译器。标准 Squirrel 语法是 EveScript 的兼容子集，
因此现有脚本、上游示例和第三方 `.nut` 可以不加标记直接使用。编译器生成 Squirrel 字节码、
SourceMap 和 Metadata，供 VM、错误栈、LSP、Inspector、MCP、热重载与打包器使用。

一项语法只有同时满足以下条件才进入语言：

1. 能减少高频、易错的脚本代码，或者提供明确的编译期检查；
2. 能确定地 lowering 为现有 Squirrel 表达；
3. lowering 后没有额外的逐帧分配或隐藏调度；
4. 调试器能够把执行位置映射回原始 `.nut` 文件。

EveScript v1 包含：

- 项目级模块声明；
- `import` / `export` 脚本模块；
- 类型标注和 nullable；
- 字符串取值类型；
- `?.`、`??`、`??=`；
- 命名参数；
- Squirrel attribute 的短写；
- root 持久变量；
- 单位字面量；
- `match`；
- `async function` 和 `await`。

## 2. 文件与编译流程

### 2.1 文件识别

`config.nut` 和 `main.nut` 继续作为项目配置与默认主入口，其他脚本也统一使用 `.nut`。语言版本
属于 SDK/运行时版本，而不是单个文件的选择；项目升级编译器时一次性获得同一套语义。

所有脚本入口都默认解析 EveScript，包括文件加载、字符串编译、REPL、Debugger、MCP eval、
EditorHost 和热重载。引擎内部如果需要直接编译生成后的 Squirrel AST/字节码，那只是编译器实现
接口，不构成另一套用户可见语言模式。

### 2.2 为什么这样设计

- 一个项目只有一种脚本语义，不会因文件后缀或首行标记产生隐蔽差异；
- 标准 Squirrel 是兼容子集，现有游戏和第三方脚本不需要迁移；
- `config.nut`、`main.nut` 与现有资源布局保持稳定；
- 编辑器、运行时、打包器和调试器不需要猜测文件采用哪种语法；
- 同一个 VM 保留当前绑定、热重载、平台和 SDK 分发方式。

### 2.3 Lowering 后的等价表达

类型元数据不会生成运行时代码。编译结果仍进入当前 Squirrel VM：

```text
main.nut
  -> Eve lexer/parser
  -> 类型、脚本模块、绑定检查与 lowering
  -> Squirrel bytecode + SourceMap + ScriptMetadata
  -> current Squirrel VM
```

没有使用扩展语法的 `.nut` 应产生与当前 Squirrel 编译器等价的字节码和运行语义。编译器内部可
复用上游 lexer、parser 和 code generator，但不在用户接口上暴露“原版模式”开关。

## 3. 项目级模块声明

### 3.1 语法

模块在 `config.nut` 中为整个游戏声明：

```squirrel
config = {
    width = 1280
    height = 720
    title = "Dungeon"
    modules = ["gfx", "scene", "physics", "keyboard"]
    optionalModules = ["audio"]
}
```

数组中使用脚本 root slot 名，例如 `gfx`，而不是 CMake 模块名 `graphics`。

### 3.2 为什么这样写、这样写的好处

模块是否可用是项目级事实。同一个游戏的所有脚本共享 `gfx`、`physics` 等 root slot，把声明放在
项目配置中可以：

- 只维护一份模块要求；
- 在启动前报告 SDK/profile 缺失；
- 给项目内所有文件提供一致的补全环境；
- 让打包器从同一处计算模块依赖；
- 用 `optionalModules` 明确哪些功能需要运行时降级。

`config.modules` 不能把已经从 SDK 裁掉的模块重新启用。实际可用模块仍由
`cmake/module_manifest.cmake` 和构建 profile 决定。

为了让 LSP 无需执行任意脚本，这两个字段必须是字符串数组字面量。未写这两个字段的旧项目保持
当前行为。

### 3.3 Lowering 后的等价原始表达

`config.nut` 本身就是 Squirrel，不发生 lowering。ScriptCompiler 只读取其中的数组字面量，为
LSP、打包器和提前诊断生成项目元数据，不参与模块实例化，也不负责启动期校验。

运行时校验属于 `src/scripts/load.nut` 的加载职责：它在按照 `eve.moduleList` 实例化当前构建实际
包含的模块之后、执行 `main.nut` 之前运行：

```squirrel
if ("modules" in config) {
    foreach (slot in config.modules) {
        if (!has_module(slot))
            throw "required module is missing: " + slot
    }
}
```

可选模块继续使用现有判断：

```squirrel
if (has_module("audio")) {
    audio.play(...)
}
```

这段逻辑应直接加入 `load.nut`，并保持现有模块实例化顺序。这样编译器不依赖 Runtime 当前装载了
哪些模块，`load.nut` 也继续作为项目配置、模块实例和游戏入口之间唯一的启动编排层。

## 4. 脚本模块与 `import`

### 4.1 语法

脚本通过显式导入和导出组成模块：

```squirrel
import { Player, spawn_player } from "game:/scripts/player.nut"
import * as combat from "./combat.nut"

export class GameSession {
    players = []
}

export function start_game() {
    return spawn_player()
}

export const MAX_PLAYERS = 4
```

v1 的模块说明符必须是字符串字面量。绝对 URI 使用稳定命名空间：

- `game:/...`：游戏脚本和随游戏打包的脚本；
- `engine:/...`：SDK 提供的标准脚本库；
- `plugin:<id>/...`：插件公开的脚本模块；
- `./...`、`../...`：相对于导入者的规范 URI 解析，但不能越出所属根。

模块拥有私有环境和显式 export table。相同规范 URI 在一个 Runtime generation 内只实例化一次；
导入方不能依赖模块内部未导出的 root slot。v1 在编译依赖图时拒绝循环导入，避免暴露“部分初始化
export”的时序语义。

### 4.2 为什么这样写、这样写的好处

`dofile` 只表达“读取并执行一个文件”，没有稳定模块身份、私有命名空间、单次执行缓存、显式
依赖或导出边界，也无法让打包器在运行前完整发现脚本。`import` / `export` 提供：

- 可静态分析的依赖图，供打包裁剪、LSP、测试和热重载使用；
- 明确命名空间，避免多个脚本向 root table 注入同名 slot；
- 同一模块只执行一次，不因多处加载重复注册系统或事件；
- 与物理打包格式无关的规范 URI，脚本无需知道它来自目录、ZIP、APK 或内存；
- 可追踪的模块身份和内容哈希，使缓存、错误栈和增量重载保持一致。

`dofile` 保留为旧脚本兼容能力，但新代码以 `import` 为标准模块入口。兼容实现也应复用统一的脚本
源码读取接口，不能绕过虚拟文件系统；它仍不获得 `import` 的命名空间、缓存和依赖语义。

### 4.3 Lowering 后的等价原始表达

导入：

```squirrel
import { Player, spawn_player } from "game:/scripts/player.nut"
import * as combat from "./combat.nut"
```

等价于编译器生成的隐藏 Runtime intrinsic 调用：

```squirrel
local __module_0 = __eve_import("game:/scripts/player.nut", __eve_current_module)
local Player = __module_0.Player
local spawn_player = __module_0.spawn_player
local combat = __eve_import("./combat.nut", __eve_current_module)
```

导出模块：

```squirrel
export class Player {}
export function spawn_player() { return Player() }
```

概念上等价于：

```squirrel
__eve_define_module("game:/scripts/player.nut", function(__exports) {
    local Player = class {}
    local spawn_player = function() { return Player() }
    __exports.Player <- Player
    __exports.spawn_player <- spawn_player
})
```

`__eve_import`、`__eve_define_module` 和 `__eve_current_module` 是 lowering 的说明记号，不进入脚本
公共 API。实际实现由 Runtime 创建模块环境、执行闭包并持有 export table，脚本无法伪造模块身份。

### 4.4 EVEngine 脚本源码接口

模块加载分成“说明符解析”和“源码读取”，公共接口放在 `src/engine/common/`，不让 Runtime 反向
依赖 filesystem、plugin 或平台模块：

```cpp
enum class ScriptModuleStatus { NotHandled, Found, Error };

struct ScriptModuleRequest {
    std::string importerUri;
    std::string specifier;
};

struct ScriptModuleSource {
    std::string canonicalUri;
    std::string utf8Source;
    std::string contentHash;
    std::string debugOrigin;
};

class IScriptModuleProvider {
public:
    virtual ~IScriptModuleProvider() = default;
    virtual ScriptModuleStatus resolve(const ScriptModuleRequest& request,
                                       std::string& canonicalUri,
                                       std::string& error) = 0;
    virtual ScriptModuleStatus load(std::string_view canonicalUri,
                                    ScriptModuleSource& source,
                                    std::string& error) = 0;
};

class IScriptModuleRegistry {
public:
    static constexpr const char* capabilityName = "IScriptModuleRegistry";
    virtual ~IScriptModuleRegistry() = default;
    virtual ScriptProviderHandle registerProvider(
        std::shared_ptr<IScriptModuleProvider> provider,
        int priority) = 0;
};
```

Runtime 的 `ScriptModuleResolver` 按 scheme 和优先级管理 provider，并负责 URI 规范化、模块缓存、
依赖图、循环检测和诊断，并通过 `eve::cap::provide<IScriptModuleRegistry>()` 暴露注册入口。filesystem、
plugin 和平台模块只依赖 `src/engine/common/` 的接口，经 capability 注册 provider，不向 Runtime 添加
反向 include。`ScriptProviderHandle` 负责在提供者卸载时撤销注册，resolver 在一次解析期间使用稳定
快照。provider 只负责把逻辑 URI 映射到 UTF-8 源码，不自行执行脚本。接口用 `NotHandled` 区分
“交给下一个 provider”和真正的读取错误，避免把损坏包误报为文件不存在。

EVEngine 默认注册：

- 基于 `IFileSystem` / PhysFS 的 `game:/` provider，可透明读取开发目录、`.eve` ZIP、内存 mount、
  Android APK asset、iOS bundle 和 Web 预加载文件系统；
- SDK 内建资源的 `engine:/` provider；
- 插件注册的 `plugin:<id>/` provider；
- 开发期热重载 overlay provider，以更高优先级覆盖源码，但返回相同 canonical URI。

打包格式不进入语言语义。无论 `game:/scripts/player.nut` 最终来自散装目录还是 `.eve` 游戏包，
它都有相同模块身份、缓存键和错误栈名称。provider 返回的 `canonicalUri` 必须稳定，`contentHash`
用于判断内容 generation；不接受未经版本与完整性校验的外部预编译字节码作为 v1 模块输入。

热重载收到变更后，根据依赖图找到该模块及所有反向依赖，先在隔离环境中完成整组解析和编译，
成功后原子替换模块 generation；失败则保留旧 generation。`persist` 状态由持久存储管理，不因模块
闭包替换而丢失。

## 5. 类型标注

### 5.1 语法

类型写在变量、参数和返回值上：

```squirrel
local speed: float = 180.0
local playerName: string = "Ada"
local targets: Array<EntityRef> = []

function damage(target: EntityRef, amount: float) -> bool {
    return true
}
```

基础类型包括 `null`、`bool`、`int`、`float`、`string`。容器先支持 `Array<T>` 和
`Table<K, V>`；C++ 绑定类直接使用 Binding Contract 中的脚本类名。无法静态描述的旧代码使用
`dynamic`。

### 5.2 为什么这样写、这样写的好处

冒号和返回箭头不会改变 Squirrel 原有表达式结构，且与参数名、默认值容易区分。类型标注用于：

- 检查绑定参数数量和类型；
- 提供成员补全和重命名；
- 在脚本与原生对象边界发现错误；
- 给 Inspector、MCP 和文档提供稳定字段描述；
- 表达 nullable、字符串选项和单位。

类型是渐进式的。未标注代码继续按 Squirrel 动态语义运行，不插入全局运行时类型检查。

### 5.3 Lowering 后的等价原始表达

类型标注在生成的 Squirrel 中被擦除：

```squirrel
local speed = 180.0
local playerName = "Ada"
local targets = []

function damage(target, amount) {
    return true
}
```

编译器把类型保存在 ScriptMetadata 中。对无法静态证明安全的动态值，只在确有需要的边界插入
检查，不给普通局部变量增加包装对象。

## 6. Nullable 与空值运算

### 6.1 语法

`T?` 表示值可以为 `null`：

```squirrel
local target: EntityRef? = find_target()
target?.applyDamage(10.0)

local name = target?.displayName ?? "Unknown"
target ??= find_fallback_target()
```

### 6.2 为什么这样写、这样写的好处

原生句柄、Scene 查询、资源加载和可选模块经常返回空值。显式 nullable 可以让编译器区分“尚未
检查的空值”和普通对象，三个运算符则覆盖最常见的判空样板：

- `?.`：对象存在时才访问；
- `??`：空值使用默认值；
- `??=`：只在空值时赋值。

接收者表达式保证只求值一次，避免函数调用或属性读取产生重复副作用。

### 6.3 Lowering 后的等价原始表达

输入：

```squirrel
local name = find_target()?.displayName ?? "Unknown"
```

输出：

```squirrel
local __nullable_0 = find_target()
local __nullable_1 = (__nullable_0 == null) ? null : __nullable_0.displayName
local name = (__nullable_1 == null) ? "Unknown" : __nullable_1
```

输入：

```squirrel
target ??= find_fallback_target()
```

输出：

```squirrel
if (target == null)
    target = find_fallback_target()
```

临时变量使用编译器保留名称，不进入用户符号补全。

## 7. 字符串取值类型

### 7.1 语法

有限字符串集合用字符串字面量联合表示：

```squirrel
local mode: "idle" | "run" | "jump" = "idle"

function set_blend(mode: "opaque" | "mask" | "blend") {
    material.setSurfaceMode(mode)
}
```

Binding Contract 也可以为 C++ 的 `std::string` 参数声明相同 choices。

### 7.2 为什么这样写、这样写的好处

EVEngine 的 mode、kind、type 等接口大量使用字符串。字符串字面量联合保留这个 ABI，同时提供：

- 拼写检查；
- LSP 自动补全；
- `match` 穷尽检查；
- Inspector combo 选项；
- JSON、日志和插件接口的直接可读性。

运行时仍然是普通字符串，不需要在 Squirrel 与 C++ 之间转换整数枚举。

### 7.3 Lowering 后的等价原始表达

类型被擦除，传给 C++ 的值不变：

```squirrel
local mode = "idle"

function set_blend(mode) {
    material.setSurfaceMode(mode)
}
```

当参数是字符串字面量时，非法值在编译期报错；当参数来自动态输入时，仍由原生绑定或显式脚本
校验处理。

## 8. 命名参数

### 8.1 语法

命名参数在调用位置使用 `name: value`：

```squirrel
local world = physics.newWorld(
    gravityX: 0.0,
    gravityY: 980.0,
    sleep: true
)
```

### 8.2 为什么这样写、这样写的好处

图形、物理、UI 和生成器接口经常包含多个相同类型参数。命名参数可以：

- 说明每个数字和布尔值的含义；
- 避免相邻参数次序写反；
- 允许按名称重排参数；
- 在编译期发现未知、重复和缺失参数。

使用冒号而不是 `name = value`，避免与 Squirrel 已有的赋值表达式混淆。

命名参数依赖 Binding Contract。每个绑定必须提供参数名、脚本类型、是否 nullable、脚本默认值和
单位。C++ 默认实参不会自动成为脚本默认值。

### 8.3 Lowering 后的等价原始表达

编译器按 Binding Contract 还原位置参数：

```squirrel
local world = physics.newWorld(0.0, 980.0, true)
```

对于脚本函数，参数顺序直接来自函数声明。对于原生绑定，缺少可靠 Binding Contract 时禁止使用
命名参数，仍可使用原始位置调用。

## 9. 属性短写

### 9.1 语法

成员元数据使用注解形式：

```squirrel
class CharacterData {
    @editor("slider", min: 0, max: 100, step: 1)
    @unit("hp")
    hp: float = 100.0

    @editor("combo")
    job: "warrior" | "mage" | "rogue" = "warrior"
}
```

### 9.2 为什么这样写、这样写的好处

EVEngine 已经能反射 Squirrel attribute 并驱动 Inspector。短写只改善较长元数据的可读性，同时
让类型信息和字符串 choices 自动补齐编辑器描述：

- 不增加第二套属性系统；
- Inspector、MCP 和运行时反射读取同一份数据；
- 注解可以逐行组合，diff 清晰；
- 注解参数复用命名参数的冒号规则。

### 9.3 Lowering 后的等价原始表达

输入：

```squirrel
@editor("slider", min: 0, max: 100, step: 1)
@unit("hp")
hp: float = 100.0
```

输出：

```squirrel
</ editor = "slider", min = 0, max = 100, step = 1, unit = "hp" />
hp = 100.0
```

字符串 choice 类型与 `@editor("combo")` 组合时，编译器追加现有 Inspector 使用的 options：

```squirrel
</ editor = "combo", options = "warrior,mage,rogue" />
job = "warrior"
```

未知注解是编译错误，除非它属于已注册的插件命名空间。

## 10. 持久变量

### 10.1 语法

需要跨脚本热重载保留的 root 变量使用 `persist`：

```squirrel
persist score: int = 0
persist session: Table<string, dynamic> = {}
```

未标注变量就是普通 Squirrel 变量。`persist` v1 只允许出现在文件 root scope，不用于类字段或
局部变量。

### 10.2 为什么这样写、这样写的好处

当前脚本常用 root table 判断或 `persist(name, init)` 保存热重载状态。声明形式可以：

- 消除重复的字符串名称和初始化闭包；
- 只在第一次加载时执行初始化表达式；
- 让热重载工具明确知道哪些 root 是有意保留的；
- 对重复名称、不可保存的原生句柄给出诊断。

普通变量不需要 `transient` 标记；没有 `persist` 就不获得跨重载保留承诺。

### 10.3 Lowering 后的等价原始表达

输入：

```squirrel
persist score: int = 0
```

输出：

```squirrel
score <- persist("score", function() {
    return 0
})
```

名称必须在 root table 中唯一。初始化表达式的副作用与现有 `persist()` 完全一致：只有对应 root
尚不存在时才执行。

## 11. 单位字面量

### 11.1 语法

v1 支持游戏中最常见的时间、角度和空间单位：

```squirrel
local fade: seconds = 250ms
local turn: radians = 90deg
local tileSize: pixels = 32px
local height: meters = 1.8m

await asyncSleep(250ms)
camera.setYaw(90deg)
```

### 11.2 为什么这样写、这样写的好处

EVEngine API 同时使用秒/毫秒、弧度/角度、像素/米。裸浮点数无法表达单位，容易出现数值正确但
量纲错误的问题。单位字面量可以：

- 在调用前检查量纲；
- 自动完成确定的比例转换；
- 在 hover 和文档中显示绑定要求；
- 不创建运行时单位对象。

Binding Contract 必须记录原生参数的目标单位。没有单位信息的动态调用不会自动转换。

### 11.3 Lowering 后的等价原始表达

赋给明确类型时转换为该类型的标量：

```squirrel
local fade = 0.25
local turn = 1.5707963267948966
local tileSize = 32.0
local height = 1.8
```

传给绑定时转换为绑定要求的单位：

```squirrel
// asyncSleep 的参数单位是 millisecond
await asyncSleep(250.0)

// Camera.setYaw 的参数单位是 radian
camera.setYaw(1.5707963267948966)
```

这里的 `await` 还会继续按第 13 节 lowering；单位转换先于异步转换。

## 12. `match`

### 12.1 语法

v1 的 `match` 是语句，不是表达式：

```squirrel
function update_mode(mode: "idle" | "run" | "jump") {
    match mode {
        "idle" => update_idle()
        "run"  => update_run()
        "jump" => update_jump()
    }
}
```

普通 `string`、`int` 或 `dynamic` 必须提供 `else`：

```squirrel
match input {
    "yes" => accept()
    "no"  => reject()
    else  => report_invalid(input)
}
```

### 12.2 为什么这样写、这样写的好处

状态、输入动作、资源类型和事件类型经常由字符串区分。`match` 与字符串 choice 类型结合后可以：

- 检查是否覆盖全部选项；
- 防止分支拼写错误；
- 避免重复书写被比较的表达式；
- 保持分支结构比长 `if/else if` 更清楚。

v1 只做字面量匹配，不加入解构、guard 或模式绑定，确保 lowering 简单可预测。

### 12.3 Lowering 后的等价原始表达

```squirrel
function update_mode(mode) {
    switch (mode) {
        case "idle":
            update_idle()
            break
        case "run":
            update_run()
            break
        case "jump":
            update_jump()
            break
    }
}
```

带 `else` 的分支 lowering 为 `default`。subject 表达式只求值一次。

## 13. `async function` 与 `await`

### 13.1 语法

```squirrel
async function load_round() -> string {
    await asyncSleep(50ms)
    local result: string = await asyncDelay(20ms, "ready")
    return result
}

load_round().then(function(result) {
    print(result + "\n")
})
```

`async function` 总是返回现有 `Promise`。`await` 接受 Promise/thenable；普通值等价于已经 resolve
的 Promise。异步 continuation 仍由当前 `async_pump()` 在主线程推进。

### 13.2 为什么这样写、这样写的好处

当前 `Promise.then` 和 `asyncSeq` 可以表达顺序异步，但多步骤流程需要拆成多个闭包。`await` 的
收益是：

- 顺序逻辑保持从上到下阅读；
- 局部变量不必手动跨闭包传递；
- `try/catch`、返回值和异常可以由编译器统一转换为 Promise resolve/reject；
- 不改变线程模型，不把 Squirrel VM 代码移动到 worker。

`await` 不阻塞帧循环，也不会自动等待 `eve_update`。调用者必须保存或继续处理返回的 Promise。

### 13.3 Lowering 后的等价原始表达

上面的线性函数等价于当前 `asyncSeq`：

```squirrel
function load_round() {
    return asyncSeq([
        function(_) {
            return asyncSleep(50.0)
        },
        function(_) {
            return asyncDelay(20.0, "ready")
        },
        function(result) {
            return result
        }
    ])
}
```

包含循环、分支和 `try/catch` 的 async function lowering 为编译器生成的 Promise 状态机；每个
`await` 是一个暂停点。状态机必须保持以下等价规则：

- `return value` -> resolve(value)；
- 未捕获异常 -> reject(error)；
- awaited Promise rejected -> 在对应源码位置抛出；
- continuation 通过 `nextTick`/`async_pump` 回到游戏主线程；
- SourceMap 把生成状态映射回原始函数和行号。

## 14. `const`

### 14.1 语法

继续使用 Squirrel 原有写法：

```squirrel
const MAX_PLAYERS = 4
const PI = 3.14159265358979
```

### 14.2 为什么这样写、这样写的好处

`const` 用于给编译期标量常量命名，减少 magic number，并允许编译器做常量折叠。它不表示深度
不可变对象，不检查数组或 table 内部修改，也不参与持久化语义。

### 14.3 Lowering 后的等价原始表达

这是现有 Squirrel 语法，EveScript 原样传递：

```squirrel
const MAX_PLAYERS = 4
const PI = 3.14159265358979
```

不生成额外运行时代码。

## 15. 完整示例

### 15.1 EveScript 输入

`config.nut`：

```squirrel
config = {
    width = 960
    height = 540
    title = "Typed Physics"
    modules = ["gfx", "physics", "timer"]
}
```

`scripts/game_state.nut`：

```squirrel
export class GameState {
    @editor("combo")
    mode: "idle" | "running" | "finished" = "idle"
    launches: int = 0
}
```

`main.nut`：

```squirrel
import { GameState } from "game:/scripts/game_state.nut"

const START_X = 120.0

persist gameState: GameState = GameState()

world: eve.World? <- null

async function begin_after(delay: milliseconds) -> string {
    await asyncSleep(delay)
    return "running"
}

function set_mode(next: "idle" | "running" | "finished") {
    gameState.mode = next
    match gameState.mode {
        "idle"     => print("idle\n")
        "running"  => print("running\n")
        "finished" => print("finished\n")
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0)
    world = physics.newWorld(
        gravityX: 0.0,
        gravityY: 980.0,
        sleep: true
    )

    begin_after(250ms).then(function(next) {
        gameState.launches += 1
        set_mode(next)
    })
}

eve_update = function(dt: seconds) {
    world?.update(dt)
}

eve_render = function() {
    gfx.clear()
}
```

### 15.2 Lowering 后的普通 Squirrel

`scripts/game_state.nut` 的模块闭包：

```squirrel
__eve_define_module("game:/scripts/game_state.nut", function(__exports) {
    local GameState = class {
        </ editor = "combo", options = "idle,running,finished" />
        mode = "idle"
        launches = 0
    }
    __exports.GameState <- GameState
})
```

`main.nut`：

```squirrel
local __module_0 = __eve_import("game:/scripts/game_state.nut", __eve_current_module)
local GameState = __module_0.GameState

const START_X = 120.0

gameState <- persist("gameState", function() {
    return GameState()
})

world <- null

function begin_after(delay) {
    return asyncSeq([
        function(_) {
            return asyncSleep(delay)
        },
        function(_) {
            return "running"
        }
    ])
}

function set_mode(next) {
    gameState.mode = next
    switch (gameState.mode) {
        case "idle":
            print("idle\n")
            break
        case "running":
            print("running\n")
            break
        case "finished":
            print("finished\n")
            break
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0)
    world = physics.newWorld(0.0, 980.0, true)

    begin_after(250.0).then(function(next) {
        gameState.launches += 1
        set_mode(next)
    })
}

eve_update = function(dt) {
    local __nullable_0 = world
    if (__nullable_0 != null)
        __nullable_0.update(dt)
}

eve_render = function() {
    gfx.clear()
}
```

除了生成的临时变量、Promise 闭包和属性表，代码继续调用当前模块、函数和帧入口。

## 16. 编译器依赖的元数据

语法 lowering 依赖两份由引擎生成的契约。

### 16.1 Module Contract

从 `cmake/module_manifest.cmake` 生成：

```text
module name
root slot
script class
required and optional dependencies
profiles and platforms
```

它供 `load.nut` 的运行时校验、LSP/打包器的静态校验以及 root module 补全共同使用。

### 16.2 Binding Contract

从 Squirrel 绑定声明生成：

```text
module / class / method
parameter order / name / type / nullable / script default / unit / choices
return type / nullable / ownership
thread affinity
platform availability
documentation id
```

它用于类型检查、命名参数、单位转换、字符串补全和 API 文档。绑定没有声明脚本默认值时，调用者
必须传入完整参数。

### 16.3 ScriptMetadata

每个 `.nut` 模块编译产物记录：

```text
language version and source hash
canonical module URI, provider origin, imports and exports
dependency and reverse-dependency edges
declared symbols and erased types
attributes and property choices
persist roots
module references
async functions and await source locations
generated symbol map
```

Runtime 不需要在每帧读取这些信息；它们主要供加载、热重载、Inspector、LSP、MCP 和打包阶段
使用。

## 17. 诊断规则

v1 至少提供以下诊断：

```text
EVE1001 config.modules contains an unknown root slot
EVE1002 required module is absent from the selected profile
EVE1101 cannot resolve imported module
EVE1102 import escapes its module root
EVE1103 cyclic import is not supported
EVE1104 requested export does not exist
EVE1105 provider returned inconsistent module identity
EVE2001 nullable value must be checked before member access
EVE2002 value is not assignable to the declared type
EVE2101 string is outside the allowed choices
EVE2201 unknown, duplicate, or missing named argument
EVE2202 named arguments require a Binding Contract
EVE2301 incompatible units
EVE2401 non-exhaustive match
EVE2501 persist is only allowed at root scope
EVE2502 persist initializer contains an unsupported native handle
EVE2601 await is only allowed inside async function
```

所有诊断包含原始 `.nut` 文件、行列、规范模块 URI、相关绑定或配置位置，以及可以确定生成的修复
建议。模块加载错误还应列出导入者、原始 specifier 和实际参与解析的 provider。

## 18. 实施顺序

### Phase 0：契约和统一入口

1. 建立 ScriptCompiler，统一文件加载、字符串编译、热重载、REPL、MCP 和 EditorHost 编译路径；
2. 定义 `IScriptModuleProvider`、`ScriptModuleResolver`、规范 URI 和 provider 注册机制；
3. 接入 PhysFS / `IFileSystem` provider，覆盖目录、`.eve` 游戏包和平台虚拟文件系统；
4. 实现模块私有环境、export table、单次执行缓存、依赖图和原子 generation 替换；
5. 从 module manifest 生成 Module Contract；
6. 为绑定补充参数名、类型、单位、choices 和脚本默认值；
7. 定义 SourceMap、ScriptMetadata 和诊断格式；
8. 建立整个仓库现有 `.nut` 的源码兼容与字节码语义基线。

### Phase 1：无控制流语法

1. `import` / `export` 解析、lowering、循环诊断和打包依赖发现；
2. 在 `load.nut` 中实现项目模块运行时验证，并在 LSP 中提供对应静态诊断；
3. 类型标注和字符串 choice 类型；
4. nullable 与 `?.`、`??`、`??=`；
5. 命名参数；
6. 属性短写；
7. root `persist`；
8. 单位字面量；
9. LSP 补全、hover 和诊断。

### Phase 2：控制流语法

1. `match` 与穷尽检查；
2. `async function`、`await` 和 Promise 状态机；
3. DAP 对 lowering 代码的断点、单步和异常映射；
4. 热重载时 async continuation 的诊断和清理策略。

每个 Phase 都必须先把生成的 Squirrel 作为可查看调试产物输出，再启用对应语法。

## 19. 验收标准

### 19.1 兼容性

1. 仓库现有 `.nut` 不修改即可继续运行；
2. 标准 Squirrel 文件与使用扩展语法的文件无需标记即可互相导入；
3. 同一规范 URI 从目录、ZIP、APK、内存或 Web VFS 加载时具有相同模块语义；
4. Windows、Linux、macOS、Web 和移动端继续使用同一个 Squirrel VM；
5. SDK 游戏仍然只需要 `config.nut`、`main.nut` 和其他脚本文件。

### 19.2 语义

1. 每个语法测试同时保存 EveScript 输入和期望 Squirrel 输出；
2. 接收者、参数和初始化表达式不会因 lowering 被重复求值；
3. 类型、choice 和单位在生成代码中不产生包装对象；
4. async continuation 只由现有主线程 `async_pump()` 推进；
5. SourceMap 能把编译错误、运行异常和断点映射到原始 `.nut` 行列；
6. 模块只执行一次，循环导入在执行前失败，热重载失败时旧 generation 仍可运行。

### 19.3 性能

1. 类型、命名参数、choice、attribute 和单位在运行时零额外开销；
2. nullable lowering 只创建必要的局部临时值；
3. 没有 `await` 的函数不会生成 Promise；
4. 未使用扩展语法的 `.nut` 不产生额外运行时包装或调度；
5. import 使用 canonical URI 缓存，不重复解析和执行模块；
6. ScriptMetadata 和依赖图不在逐帧路径中扫描。

## 20. 最终语法清单

EveScript v1 新增的 token 或语法形式只有：

```text
import { name } from "module"
import * as name from "module"
export declaration
: Type
-> Type
T?
"a" | "b"
?.
??
??=
name: value
@annotation(...)
persist name = value
number + unit suffix
match / => / else
async function / await
```

其余类、继承、闭包、生命周期函数、模块调用和数据结构继续使用 Squirrel 原始表达。语言的价值
由更早的错误、更清楚的调用和可验证的 lowering 衡量，而不是由新增关键字数量衡量。
