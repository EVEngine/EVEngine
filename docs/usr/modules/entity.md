# 脚本 ECS模块

**脚本入口：** `eve.Component` / `eve.Entity` / `eve.System`

通过 Component、Entity 和 System 声明数据组合与批量更新逻辑。

## 基本用法

```squirrel
class Position extends eve.Component { x = 0.0; y = 0.0; }
class Actor extends eve.Entity { pos = Position; }
local actor = Actor.create();
actor.pos.x = 100;
```

## 对象关系与调用时机

Component 是纯数据定义；Entity 声明组件组合；`Entity.create()` 创建实例；System 用目标 Entity 类型建立查询并迭代 `entities()`。系统执行顺序由游戏主循环显式决定。

## 目标导向指南

### 定义可移动实体

分别声明 Position、Velocity Component，在 Entity 类中以字段组合它们；创建继承 `eve.System` 的移动系统，并在 `update(dt)` 遍历 `entities()`。数据放组件，行为放系统。

### 销毁和查询实体

长期保存实体引用前要明确其生命周期；系统查询只返回满足组件组合的实体。添加新的行为优先新增小组件和系统，避免形成包含所有字段的巨型 Actor。

## 常见问题

- Component 放复杂副作用：组件应保持可序列化数据。
- 多个系统隐式依赖顺序：在 `eve_update` 明确排列。
- 热重载重新定义类后混用旧实例：保留状态时也要规划迁移。

## API 快查

脚本 ECS 的主要接口由脚本基类提供：`Component` 声明数据、`Entity.create()` 创建实体、`System.entities()` 查询匹配实体；参见可运行的 `examples/ecs/main.nut`。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/entity/`](../../../src/modules/entity/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `entity`。
