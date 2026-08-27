# Schema

`eve.Schema()` 暴露运行时 Schema 注册与校验 facade。Schema id 与版本由
`SchemaRegistry` 管理；需要严格迁移或工具契约的 native 调用使用
`SchemaRegistry` 的 `Result` API。

## Script API

- `getName()`：返回模块名 `Schema`。
- `registerJson(json)`：注册或替换一个 schema；兼容旧的 `version` 字段。
- `clear()`：清空 registry 和校验诊断。
- `has(id)`、`hasVersion(id, version)`：查询 schema 是否存在。
- `getSchemaCount()`、`getSchemaVersionCount(id)`、`getSchemaVersionAt(id, index)`、
  `getSchemaId(index)`、`getSchemaVersion(id)`：读取稳定的 schema/版本枚举。
- `getSchemaTitle(id)`、`getSchemaDescription(id)`、
  `getSchemaAdditionalProperties(id)`：读取 schema 元数据。
- `getFieldCount(id)`、`getFieldName(id, index)`、`getFieldType(id, index)`、
  `getFieldElementType(id, index)`、`getFieldRequired(id, index)`：读取字段结构。
- `getFieldTitle(id, index)`、`getFieldDescription(id, index)`、
  `getFieldReference(id, index)`、`getFieldDefaultJson(id, index)`：读取工具元数据。
- `getFieldHasMinimum(id, index)`、`getFieldMinimum(id, index)`、
  `getFieldHasMaximum(id, index)`、`getFieldMaximum(id, index)`：读取数值约束。
- `getFieldEnumCount(id, index)`、`getFieldEnumValue(id, fieldIndex, valueIndex)`：读取字符串枚举。
- `validateJson(id, json)`：按最高版本校验，返回公共 Result 投影并缓存诊断。
- `validateJsonVersioned(id, version, json)`：按精确版本校验，返回公共 Result 投影；失败明细同时缓存，
  可从 `result.diagnostics[0].path/code/message` 读取，不需要查询共享错误字符串才能判断失败。
- `getValidationErrorCount()`、`getValidationErrorPath(index)`、
  `getValidationErrorCode(index)`、`getValidationErrorMessage(index)`：读取最近一次校验错误。

新 binding 必须同步更新本章，并运行 `python3 scripts/check_bindings.py --strict`。
