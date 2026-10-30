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

插件支持在同一进程、同一脚本 VM 中调用 `reload(path)` 切换代码版本。每次加载使用
唯一的动态库副本，因此原始文件可以重新编译、替换，系统加载器也不会复用旧镜像。
相对路径与绝对路径会规范化为同一个插件身份。

新版本加载、注册和绑定全部成功后，`eve` 中本插件的类才切换到新版本。
新建模块使用新实例；已有实例、缓存的类构造器和方法闭包继续使用各自的旧版本。
初始化、注册冲突或绑定失败时返回失败 Result，旧版本继续可用。

## 目标导向指南

### 加载平台匹配的插件

先用相同平台和构建配置的 EVEngine SDK 编译动态库，再调用 `load(path)`；扩展名分别为 `.dll`、`.so`、`.dylib`。加载成功后插件注册的新类会出现在 `eve` 表中。

### 排查加载失败

检查 `isLoaded(path)`、架构、Debug/Release ABI 和依赖库搜索路径。插件初始化只能注册模块，不应假设窗口或 Graphics 已完成初始化。

### 在运行中更新 C++ 插件

```squirrel
local plugins = eve.Plugins();
plugins.load("plugins/gameplay.so");
local gameplay = eve.Gameplay();

// 编译并替换原路径的动态库后，在主线程的更新边界调用：
local result = plugins.reload("plugins/gameplay.so");
if (result.ok) {
    gameplay = eve.Gameplay(); // 取得新版本实例，重新连接本插件的回调和资源
} else {
    print(result.status.summary + "\n"); // 保留当前 gameplay
}
```

`reload` 是显式触发，不自动监视源码或运行编译器。不要每帧调用；可以由开发工具、
文件监视器或脚本重载回调触发。建议先输出到临时文件，链接成功后再替换原文件，
避免读取到编译中的半成品。插件目录需要可写，以便创建同目录镜像并保留相对依赖库路径。

热重载插件须使用当前 SDK 的 `add_eve_plugin()` 重新编译：它隔离插件的 C++ 符号，
新的 `Module_IMPL` 按工厂代次管理实例，脚本绑定按工厂地址区分模块类型。
模块返回自身或其他本代模块时保留本代绑定；新代模块方法会拒绝旧代模块实例。
旧 SDK 编译的插件不能提供该代次隔离保证。插件 exposer 只能在传入表中新建本插件
的类与 slot，不能修改已有共享类/对象或执行有外部副作用的脚本；初始化器也只能注册模块。
这些外部副作用不属于加载事务的回滚范围。

不自动迁移 C++ 实例状态。需要保留的数据应由脚本或稳定的宿主数据结构持有，重载后
显式恢复并重新获取插件对象。插件自定义的非模块类型仍按名称匹配，不提供模块的
代次检查；不要把插件自有 C++ 对象传给其他代次的方法。跨代接口
使用字符串、数值等值数据或稳定的宿主类型。若插件注册了外部服务/后台线程，插件本身
还须在切换边界停止旧服务并重新连接；替换模块工厂不会自动迁移这些外部注册。

## 常见问题

- 用错平台或架构的动态库。
- Debug 插件加载进 Release 宿主。
- 插件依赖库不在运行时搜索路径。

## 绑定与行为说明

`loadPlugin(path)` 与 `reload(path)` 在 C++ 返回 `Result<void>`，在脚本返回公共 Result 表。
兼容接口 `load(path)` 成功返回 `true`、失败抛出异常；对已加载路径再次调用是无操作，
不会更新代码。`reload(path)` 要求该路径已经加载；未加载时返回 NotFound。

`unload()` 是兼容保留接口，始终返回 `false`。热重载通过发布新代次实现，不物理卸载
仍可能被引用的旧代码。动态库镜像及旧实例保留到进程结束，每次重载会增加驻留内存；
类绑定保留到 VM 销毁。Windows 下驻留镜像文件 `.eve-<pid>-<序号>-<库名>` 在进程退出后
才能删除；POSIX 在载入后立即移除镜像文件。不要用 `unload()` 实现更新。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。

**源码：** [`src/modules/plugins/`](../../../src/modules/plugins/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `plugins`。

## SSUI 素材生成插件

项目内插件见 [plugins/ssui-generation](../../../plugins/ssui-generation/README.md)。通过 `plugins.load()` 注册 `eve.SSUIGeneration()`，提供异步生成、进度、取消、PNG 下载校验及材质应用/撤销；附带原生材质面板。运行时不依赖 Python 或 LLM，采用 SSUI 开放生成协议。构建、安装、打包与生命周期限制见插件 README。
