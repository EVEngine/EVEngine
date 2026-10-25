# Squirrel Binding Style

[中文](../../dev/Squirrel绑定风格.md) · [Result contracts](RESULT-CONTRACTS.md) · [Developer index](README.md)

Binding translation units (`*Bindings.cpp` / `*ScriptBindings.cpp`) share scaffolding rather than copying `{ok,message}` tables, `bindingFailure` or null-self handling.

## Layers

- Common `SquirrelBinding.h`: the single Result/Status/Value projection.
- Common `SquirrelBindContext.h`: `BindContext` (VM and diagnostic source), `bindMethod` and `bindNullSafe`.
- Editor `EditorScriptProjection.h`: `ScriptBind`, embedding BindContext, with workspace/history/owned-create registration helpers.

Non-editor domains such as animation directly use `eve::script::BindContext`. Editor facades use `eve::editor::ScriptBind` and obtain common functionality through `bind.context()`.

Do not create a top-level `editing_script` module merely to share scaffolding. Keep the Squirrel kit in editor/common. Reconsider splitting only when another consumer cannot live in an existing satellite directory. Runtime profiles already exclude editing/editor leaves; an independent kit only adds manifest noise.

## Result projection

Use `script::projectResult` / `projectStatusResult` for success/failure. Do not hand-build `result.set("ok", ...)`. `make check/adhoc-result-tables` maintains a shrink-only allowlist for remaining gameplay compatibility code.

Script callers read `result.ok`, `result.status.summary`, `result.diagnostics` and `result.value`. Do not rely on legacy top-level `message`, `workers` or `count` fields.

## Literal registration names

Literal names allow Binding Contracts extraction:

```cpp
script::bindMethod(cls, "setEnabled", &OrientationWarping::setEnabled);
cls.addFunc("configureWorkspace", ...);  // Both forms are recognized.
```

The generator recognizes `receiver.addFunc("literal", ...)`, `script::bindMethod(cls, "literal", ...)` and `bindNullSafe(...)`. Runtime-computed names are not dependable inputs for generated contracts and can enter the unresolved list.

## Null self and failure

```cpp
const script::BindContext bind{table.getHandle(), "animation.bindings"};
return bind.failInvalid("pose is required", "pose");
return bind.checked(self, "editor must not be null", [&] { return self->apply(); });
```

These illustrate alternative helper calls, not one function containing sequential returns. Editor `ScriptBind::history` and `ownedCreate` specifically support undo history/factory creation.

## Structured reads

Keep atomic getters such as `getSelectedId` and `getRevision` unchanged. Add a snapshot API such as `getState()` returning a Value object whose fields agree with existing getters. Do not change an old getter's return shape.

## Checklist

1. Does the binding use projectResult/BindContext, or editor ScriptBind?
2. Is its name a string literal recognized by addFunc/bindMethod?
3. Are public script changes reflected in module usage docs and Binding Contracts?
4. Do `make check/adhoc-result-tables` and affected domain tests pass?

C++ defaults do not automatically become script defaults. Verify actual signatures in the SDK catalog and test script argument/return behavior.
