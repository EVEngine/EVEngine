# Squirrel 绑定风格

> 配套：[`Result检查与不得丢弃返回值规范.md`](./Result检查与不得丢弃返回值规范.md)、
> [`模块编排与裁剪架构.md`](./模块编排与裁剪架构.md)。

本文约定脚本绑定 TU（`*Bindings.cpp` / `*ScriptBindings.cpp`）如何共用脚手架，避免再复制
`{ok,message}` 表、`bindingFailure` 或 null-self 样板。

## 分层

| 层级 | 头文件 | 用途 |
|------|--------|------|
| common | `SquirrelBinding.h` | 唯一 Result / Status / Value 投影 |
| common | `SquirrelBindContext.h` | `BindContext`（VM + diagnostic source）、`bindMethod` / `bindNullSafe` |
| editor | `EditorScriptProjection.h` | `ScriptBind`（嵌入 `BindContext`）+ workspace / history / owned-create 注册器 |

非 editor 域（如 animation）直接用 `eve::script::BindContext`。editor facade 用
`eve::editor::ScriptBind`，需要 common 能力时取 `bind.context()`。

**不要**为了共用脚本脚手架新建顶层 `editing_script` 模块。Squirrel kit 留在
`editor`（及 common）侧；等出现第二个、且无法放进现有卫星目录的消费方时再评估拆包。
运行时 profile 已按叶名排除 `editing` / `editor`，把 kit 下沉到独立包只会增加清单噪声。

## Result 投影

- 成功/失败一律 `script::projectResult` / `projectStatusResult`。
- 禁止手写 `result.set("ok", …)`；Source quality 的 `make check/adhoc-result-tables`
  用 shrink-only allowlist 盯住剩余 gameplay 兼容面。
- 脚本侧读 `result.ok`、`result.status.summary`、`result.diagnostics`、`result.value`，
  不要依赖顶层 `message` / `workers` / `count` 等旧字段。

## 注册方法

优先字面量名字，便于 Binding Contracts 抓取：

```cpp
script::bindMethod(cls, "setEnabled", &OrientationWarping::setEnabled);
cls.addFunc("configureWorkspace", ...);  // 等价；Contracts 两种都认
```

`scripts/generate_binding_contracts.py` 会刮取：

- `receiver.addFunc("literal", …)`
- `script::bindMethod(cls, "literal", …)` / `bindNullSafe(…)`

运行时拼出来的方法名会进 unresolved 列表，CI 的 contracts 生成不应靠它们。

## Null-self 与失败

```cpp
const script::BindContext bind{table.getHandle(), "animation.bindings"};
return bind.failInvalid("pose is required", "pose");
return bind.checked(self, "editor must not be null", [&] { return self->apply(); });
```

editor 的 `ScriptBind::history` / `ownedCreate` 只服务撤销栈与 factory create。

## 结构化读取（兼容原子 getter）

原子 `getSelectedId` / `getRevision` 等保持不变。新增快照用加法 API，例如
`getState()` 返回 `Value` 对象（字段与现有 getter 对齐），不要改旧方法的返回形状。

## 检查清单

1. 新绑定是否走 `projectResult` / `BindContext`（或 `ScriptBind`）？
2. 方法名是否为字符串字面量（`addFunc` / `bindMethod`）？
3. 若改了公共脚本面：更新模块用户文档 + Binding Contracts。
4. `make check/adhoc-result-tables` 与相关域测试是否仍绿？
