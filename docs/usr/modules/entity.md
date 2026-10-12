# 脚本 ECS：数据、实体与系统

**脚本入口：** `eve.Component` / `eve.Entity` / `eve.System` / `eve.ShaderSystem`

通过 Component、Entity 和 System 声明数据组合与批量更新逻辑。大批量数值积分可用 `eve.ShaderSystem` 写成 compute shader（见 [gpgpu.md](gpgpu.md)）。

## 基本用法

这些基类由游戏脚本环境提供，不需要自己构造 EntityModule。Component 是正交数据，
Entity 声明组件组合，System 是对明确范围的处理。小型游戏可先用普通表；需要重复处理
实体或对接领域模型时再引入 ECS，不要创建包办战斗、背包、UI 和物理的万能 Actor。

```squirrel
class Position extends eve.Component { x = 0.0; y = 0.0; }
class Actor extends eve.Entity { pos = Position; }
local actor = Actor.create();
actor.pos.x = 100;
```

## 对象关系与调用时机

### 完整 main.nut：移动并销毁实体

配套配置为 `config = { width=960, height=540, modules=["gfx"] }`。
`position = Position` 声明组件类型；create 后才得到每实体独立的组件实例。

```squirrel
class Position extends eve.Component { x = 80.0; y = 220.0; }
class Velocity extends eve.Component { x = 120.0; y = 0.0; }
class Mover extends eve.Entity { position = Position; velocity = Velocity; }

class MoveSystem extends eve.System {
    constructor() { base.constructor(Mover); }
    function update(dt) {
        local pending = [];
        foreach (actor in entities()) {
            actor.position.x += actor.velocity.x * dt;
            actor.position.y += actor.velocity.y * dt;
            if (actor.position.x > config.width) pending.append(actor);
        }
        foreach (actor in pending) actor.destroy();
    }
}

local mover = null;
local move_system = null;
eve_init = function() {
    mover = Mover.create();
    move_system = MoveSystem();
    assert(mover.isAlive());
    assert(mover.hasComponent(Position));
    assert(mover.getComponent(Position) == mover.position);
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0);
};
eve_update = function(dt) { move_system.update(dt); };
eve_render = function() {
    gfx.clear();
    foreach (actor in eve.view(Mover))
        gfx.drawSolidRect(actor.position.x, actor.position.y, 32.0, 32.0, 0.3, 0.75, 1.0, 1.0);
};
eve_quit = function() {
    if (mover != null && mover.isAlive()) mover.destroy();
};
```

预期：蓝色方块向右移动，越出屏幕后从查询和画面中消失。先完成遍历，再销毁实体。
`pending` 是游戏层的延迟队列，不是新增引擎 API；较多实体时重用队列以减少分配。
本系统查询 Mover，读取 Velocity、读写 Position，结构变化发生在遍历后，没有其他服务
或事件。多个系统由主循环按输入 → 移动 → 碰撞/玩法 → UI 投影顺序显式推进。

Component 是纯数据定义；Entity 声明组件组合；`Entity.create()` 创建实例；System 用目标 Entity 类型建立查询并迭代 `entities()`。系统执行顺序由游戏主循环显式决定。`ShaderSystem` 继承 System，在 `update(dt)` 中打包 float 字段到 SSBO 并 `Gpgpu.dispatch`。

## 目标导向指南

### 定义可移动实体

分别声明 Position、Velocity Component，在 Entity 类中以字段组合它们；创建继承 `eve.System` 的移动系统，并在 `update(dt)` 遍历 `entities()`。数据放组件，行为放系统。

### 用 shader 写移动系统

```squirrel
local sys = eve.ShaderSystem(Moveable, gpgpu, moveGlsl)
sys.bindFields(0, "pos", ["x", "y"])
sys.bindFields(1, "vel", ["x", "y"])
```

详见 [`examples/ecs/gpu_main.nut`](../../../examples/ecs/gpu_main.nut)。

### 销毁和查询实体

长期保存实体引用前要明确其生命周期；系统查询只返回满足组件组合的实体。添加新的行为优先新增小组件和系统，避免形成包含所有字段的巨型 Actor。

`eve.view(EntityType)` 返回匹配实体数组；脚本实体沿类链登记，基类查询可以覆盖派生类。
`System(TargetType)` 使用同一查询；`setQuery(query)` 可重新指定范围。当前数组查询会
拼接结果，不要假定自动去重，否则同一实体可能一帧被更新两次。

`EntityType.component(name, type)` 是定义组件槽的另一种方式，适合定义阶段，不是任意
运行时增删组件接口。`getComponent(type)` 找不到时返回 null，`hasComponent(type)`
用于检查；同类型多槽时不要依赖未规定的遍历顺序。

`getId()` 是运行时 ID，不是跨存档 UUID。`destroy()` 撤销登记并让 `isAlive()` 变为
false，重复调用不再销毁第二次；脚本变量仍可引用实例。销毁不自动取消游戏创建的音源、
订阅或异步任务。先处理附属资源和领域 Link，再在遍历结束后销毁实体。下一帧重新查询，
不要把旧查询数组当作永远有效的实时集合。

### 把受击、销毁和 HUD 接起来

生命值归战斗或属性状态所有；死亡系统把待销毁实体收集到队列，事件通知音效和 UI。
HUD 只读权威状态，不再维护另一份 HP。另一个领域已拥有位置时，通过它的同步契约更新
表现，避免两个 System 争夺同一事实。验证实体先销毁与资源/世界先销毁两种顺序。

### 热重载和存档

完整例子面向冷启动。重新定义 class 后，旧实例不会仅因 `persist` 自动换类、换字段或
更新方法。正式项目优先持久化普通数据：暂停更新 → 收集权威状态 → 验证/迁移 →
用新类型重建实体 → 重建关系和查询 → 恢复更新。失败时不能留下半个新世界。
磁盘存档还需 schema、version 和 stable ID，见[存档指南](../SAVE.md)。

### GPU System 的权威与同步

ShaderSystem 适合批量连续数值运算，不适合逐实体执行脚本回调。先确定 CPU 或 GPU 的
权威数据，再决定上传/回读频率。`requestUploadRange` 和 `requestReadbackRange` 明确
增量范围，`bindSharedFields` 和 `record` 共享驻留数据并合并提交；不能在热路径 getter
里隐藏每帧 GPU 同步。参照 [GPU ECS 示例](../../../examples/ecs/gpu_main.nut)。

## 常见问题

- Component 放复杂副作用：组件应保持可序列化数据。
- 多个系统隐式依赖顺序：在 `eve_update` 明确排列。
- 热重载重新定义类后混用旧实例：保留状态时也要规划迁移。

## API 快查

脚本 ECS 的主要接口由脚本基类提供：`Component` 声明数据、`Entity.create()` 创建实体、`System.entities()` 查询匹配实体；`ShaderSystem` 额外提供 `bindFields` / `setShaderSource` / `update`。大批量连续仿真可关闭逐帧上传和回读，通过 `requestUploadRange` / `requestReadbackRange` 增量同步；多个 GPU System 可用 `bindSharedFields` 和 `record` 共享驻留 buffer 并合并提交。参见可运行的 `examples/ecs/main.nut` 与 `examples/ecs/gpu_main.nut`。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 本章的脚本方法以 ECS.cpp 内嵌基类为准；不是 C++ Entity 方法的自动投影。
- 验证独立组件、派生查询、重复销毁、已销毁引用、遍历后删除、系统顺序和重载重建。

**源码：** [`src/engine/common/ECS.cpp`](../../../src/engine/common/ECS.cpp)、[`src/modules/gpgpu/`](../../../src/modules/gpgpu/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `ScriptECS` / `gpgpu.shaderSystem`。
