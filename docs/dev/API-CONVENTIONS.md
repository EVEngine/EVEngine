# API 使用约定

[English](../en/usr/API-CONVENTIONS.md)

## 模块和辅助对象

EVEngine 把脚本 API 放在全局 `eve` 表中。`eve.Physics()`、`eve.Audio()` 这类构造器返回模块对象；模块再创建 `World`、`Body`、`Source` 等辅助对象。调用辅助对象的方法时，必须先从相应工厂取得对象，而不是直接在模块上调用。

```squirrel
local physics = eve.Physics();                 // 模块
local world = physics.newWorld(0, 980, true); // World
local body = world.newBody("dynamic", 0, 0); // Body
```

`load.nut` 会按项目模块配置创建常用全局实例，例如 `gfx`、`win`、`keyboard`、`mouse`、`timer`、`fs`、`hot`、`physics`、`ui`。手动构造模块适用于独立脚本和需要明确依赖的代码。

## API 的可信边界

脚本能调用的方法以各模块 `expose(...)` 中的 `addFunc`、`addVar` 和脚本注入类为准，而不是以 C++ 头文件中的所有 public 方法为准。C++ 默认参数也不自动成为脚本默认值。

- C++ 声明、参数、返回与异常契约：查 [Doxygen 自动生成参考](https://evengine.github.io/EVEngine/)。
- 当前 SDK 的脚本签名：查 `share/eve/ai/eve-api.json`、编辑器补全/Hover，或 MCP `eve_api_search` / `eve_api_get`。
- 源码工作区可运行 `python3 scripts/generate_binding_contracts.py --json-output eve-api.json` 生成脚本目录，再结合对应模块测试核对调用方式。

模块手册只保留任务步骤、关键绑定差异、示例和生命周期说明，不重复维护无参数和行为说明的方法名清单。

## 错误语义

调用前请先看对应头文件 Doxygen 里的 `@throws` / `@return`，并检查脚本绑定是否改变了返回形状：

1. **编程错误 / 前置条件失败 → 抛 `eve::Exception`**：非法枚举字符串、越界索引、空对象、重复初始化（如 `Graphics::initHeadless` 二次调用）、后端未初始化等。开发阶段应让异常直接暴露，不要静默吞掉。
2. **可恢复失败 → 返回 `false` / `nullptr`**：文件或归档不存在（`Filesystem::mount`）、资源已设置过（`Filesystem::setSource` 二次调用）、可选查询越界（`ModelData::getMesh`）。这类调用通常不抛异常，必须检查返回值。
3. **结构化 Result**：先检查 `result.ok`，成功后再使用 `result.value`；失败时检查 `status.summary` / `diagnostics`。不得丢弃 Result，确需忽略时写明原因。部分旧绑定把原生 Result 投影为字符串或 `1/0`，不能直接按 C++ 返回类型调用脚本。

判定口诀：**“调用方式本身写错了”抛异常；“资源/状态不满足”返回失败**。若某个 API 与这条规则不符，视为缺陷，应在头文件 Doxygen 中明确标注实际行为。

## 生命周期和所有权

- 将模块创建的世界、纹理、声音、发射器、场景宿主等对象保存在全局状态或实体组件中。
- 不要在 `eve_render()` 中重复载入磁盘资源或创建长期对象。
- `update(dt)` 通常使用秒为单位；2D 物理常用像素坐标，3D 物理使用米，具体以模块契约为准。
- 输入和网络事件应在更新阶段消费，绘制调用放在渲染阶段。
- 热重载会重新执行脚本顶层代码，使用 EveScript 的 `persist` 声明需要保留的状态，布局变化时在 `eve_reload()` 显式迁移。

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
