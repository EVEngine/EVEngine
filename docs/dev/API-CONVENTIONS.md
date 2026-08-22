# API 使用约定

## 模块和辅助对象

EVEngine 把脚本 API 放在全局 `eve` 表中。`eve.Physics()`、`eve.Audio()` 这类构造器返回模块对象；模块再创建 `World`、`Body`、`Source` 等辅助对象。模块手册的“API 快查”同时列出模块和辅助对象方法，调用时必须先从相应工厂取得对象。

```squirrel
local physics = eve.Physics();                 // 模块
local world = physics.newWorld(0, 980, true); // World
local body = world.newBody("dynamic", 0, 0); // Body
```

`load.nut` 已为常用模块创建 `gfx`、`window`、`keyboard`、`mouse`、`event`、`timer`、`map`、`particles`、`physics`、`ui` 等全局实例。手动构造模块适用于独立脚本和需要明确依赖的代码。

## API 的可信边界

脚本能调用的方法以各模块 `expose(...)` 中的 `addFunc`、`addVar` 和脚本注入类为准，而不是以 C++ 头文件中的所有 public 方法为准。例如 `Image` 的部分 C++ 解码方法当前没有暴露给 Squirrel。模块手册的 API 快查来自实际绑定；方法的参数顺序、默认值和返回类型可继续查看链接的头文件与测试。

## 错误语义

引擎对外 API 的错误契约是**两态**的，调用前请先看对应头文件 Doxygen 里的 `@throws` / `@return`：

1. **编程错误 / 前置条件失败 → 抛 `eve::Exception`**：非法枚举字符串、越界索引、空对象、重复初始化（如 `Graphics::initHeadless` 二次调用）、后端未初始化等。开发阶段应让异常直接暴露，不要静默吞掉。
2. **可恢复失败 → 返回 `false` / `nullptr`**：文件或归档不存在（`Filesystem::mount`）、资源已设置过（`Filesystem::setSource` 二次调用）、可选查询越界（`ModelData::getMesh`）。这类调用通常不抛异常，必须检查返回值。

判定口诀：**“调用方式本身写错了”抛异常；“资源/状态不满足”返回失败**。若某个 API 与这条规则不符，视为缺陷，应在头文件 Doxygen 中明确标注实际行为。

## 生命周期和所有权

- 将模块创建的世界、纹理、声音、发射器、场景宿主等对象保存在全局状态或实体组件中。
- 不要在 `eve_render()` 中重复载入磁盘资源或创建长期对象。
- `update(dt)` 使用秒为单位；物理模块对外使用像素坐标。
- 输入和网络事件应在更新阶段消费，绘制调用放在渲染阶段。
- 热重载会重新执行脚本顶层代码，使用 `if (!("name" in getroottable()))` 保护需要保留的引用。

## 错误处理

无效枚举字符串、越界索引、空对象或后端未初始化通常会抛出 Squirrel 异常。开发阶段应让异常直接暴露；对可恢复的资源加载可以使用 Squirrel 的 `try` / `catch`：

```squirrel
try {
    level <- map.newLayerFromFile("maps/level.json");
} catch (e) {
    print("load level failed: " + e + "\n");
    level <- map.newLayer(20, 12, 32, 32);
}
```
