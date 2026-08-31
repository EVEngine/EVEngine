# 脚本 ECS模块

**脚本入口：** `eve.Component` / `eve.Entity` / `eve.System` / `eve.ShaderSystem`

通过 Component、Entity 和 System 声明数据组合与批量更新逻辑。大批量数值积分可用 `eve.ShaderSystem` 写成 compute shader（见 [gpgpu.md](gpgpu.md)）。

## 基本用法

```squirrel
class Position extends eve.Component { x = 0.0; y = 0.0; }
class Actor extends eve.Entity { pos = Position; }
local actor = Actor.create();
actor.pos.x = 100;
```

## 对象关系与调用时机

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

## 常见问题

- Component 放复杂副作用：组件应保持可序列化数据。
- 多个系统隐式依赖顺序：在 `eve_update` 明确排列。
- 热重载重新定义类后混用旧实例：保留状态时也要规划迁移。

## API 快查

脚本 ECS 的主要接口由脚本基类提供：`Component` 声明数据、`Entity.create()` 创建实体、`System.entities()` 查询匹配实体；`ShaderSystem` 额外提供 `bindFields` / `setShaderSource` / `update`。大批量连续仿真可关闭逐帧上传和回读，通过 `requestUploadRange` / `requestReadbackRange` 增量同步；多个 GPU System 可用 `bindSharedFields` 和 `record` 共享驻留 buffer 并合并提交。参见可运行的 `examples/ecs/main.nut` 与 `examples/ecs/gpu_main.nut`。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/engine/common/ECS.cpp`](../../../src/engine/common/ECS.cpp)、[`src/modules/gpgpu/`](../../../src/modules/gpgpu/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `ScriptECS` / `gpgpu.shaderSystem`。
