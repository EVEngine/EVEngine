# 原生插件模块

**脚本入口：** `eve.Plugins()`

从动态库加载用 EVEngine SDK 编译的原生模块。

## 基本用法

```squirrel
local plugins = eve.Plugins();
plugins.load("plugins/gameplay.so"); // Windows 使用 .dll，macOS 使用 .dylib
print(plugins.isLoaded("plugins/gameplay.so") + "\n");
```

## 对象关系与调用时机

`Plugins` 加载动态库；库导出的 `eve_plugin_init` 触发模块注册；随后脚本从 `eve` 创建新模块。插件二进制与宿主共享 C++ ABI。

成功加载的插件会驻留到进程结束。引擎不会在运行期间卸载插件，因为 Squirrel
类、闭包和原生模块实例可能仍引用动态库中的代码。

## 目标导向指南

### 加载平台匹配的插件

先用相同平台和构建配置的 EVEngine SDK 编译动态库，再调用 `load(path)`；扩展名分别为 `.dll`、`.so`、`.dylib`。加载成功后插件注册的新类会出现在 `eve` 表中。

### 排查加载失败

检查 `isLoaded(path)`、架构、Debug/Release ABI 和依赖库搜索路径。插件初始化只能注册模块，不应假设窗口或 Graphics 已完成初始化。

## 常见问题

- 用错平台或架构的动态库。
- Debug 插件加载进 Release 宿主。
- 插件依赖库不在运行时搜索路径。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getName()`、`isLoaded()`、`load()`、`unload()`

`unload()` 是兼容保留接口，始终返回 `false`；需要更新插件代码时请重启进程。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/plugins/`](../../../src/modules/plugins/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `plugins`。
