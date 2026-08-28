# Eve Schema 子集与绑定契约

日期：2026-08-26
状态：实现契约

## 定位

`schema::SchemaRegistry` 实现的是 Eve Schema v1。它服务于动态 JSON 内容的
准入校验、内容迁移和工具绑定生成，不承诺完整 JSON Schema 兼容性。Schema 以
`(schemaId, schemaVersion)` 为唯一键；同一个 id 的多个版本可以并存。

Schema 不是所有序列化格式的第二套解析器。SnapshotEnvelope、EventEnvelope
以及 GeneratedArtifact 等已有强类型 codec 的内部协议，继续由其 owner 在
decode/restore 边界完成结构、版本、hash 和事务校验；不得为了“使用 Schema”
再维护一份可能漂移的字段真源。

当前生产接入点包括动态 Definition/Policy metadata，以及下面两个持久化
恢复边界：

- `definitions:registry` v1：由 Definitions 模块拥有，`DefinitionRegistry::restoreJson`
  在任何 registry 状态变化前校验完整 JSON；
- `game_event:stream` v2：由 GameEvent 模块拥有，事件流恢复在解析和提交前
  校验当前版本 payload。

校验失败必须返回结构化 Diagnostic，并保持原状态不变。这里的 Schema
是实际内容准入契约，不是可选的 UI 提示。`SnapshotEnvelope` 仍由 common 中的
强类型 codec 校验，不反向依赖 schema 模块。

支持的结构与关键字：

- `type`：`any`、`null`、`boolean`、`integer`、`number`、`string`、`object`、`array`；
- 对象字段：`fields`、`required`、`additionalProperties`、`title`、`description`、`defaultJson`；
- 标量约束：`minimum`、`maximum`、`minLength`、`maxLength`、`enum`；
- 数组约束：`items`（单一递归 item schema）、`minItems`、`maxItems`；旧的
  `elementType` 仅用于兼容简单同类型数组；
- 引用：`ref` 和可选 `refVersion`，只引用完整 schema，不支持 JSON Pointer fragment；
- 联合：`union`（`oneOf` 是兼容别名）和可选 `discriminator`、
  `discriminatorMapping`。mapping 值可以是零基 variant 下标或 variant 的完整 `ref`。

不支持 `patternProperties`、`if/then/else`、`unevaluatedProperties`、tuple array、
任意关键词组合、正则表达式、JSON Pointer fragment 和自定义关键字。需要这些
能力的模块必须拥有自己的 adapter，并在边界处返回结构化 `Unsupported`，不能
把未实现的语义当作成功。

## Migration contract

迁移是显式的单向链：每个 `(schemaId, fromVersion)` 最多一个出边，且
`toVersion > fromVersion`。`queryCompatibility` 只返回精确版本或已注册的完整
路径；缺少 schema、缺少边、越过目标、循环和降级都返回 `Result` 失败。

迁移回调接收 `const eve::Value&`，必须返回新的 owning `eve::Value`。输入先按
源 schema 校验，每一步的输出按目标 schema 校验。任何回调、解析或校验失败都
不改变输入，也不改变 registry；旧版本和未知新版本不会被静默选择最近版本。

### 存档兼容窗口

- 每个已发布的持久格式必须支持当前版本以及前一个已发布版本（`N`、`N-1`）；同一发布周期内尚未发布的开发版本不计入窗口。
- 从 `N-1` 到 `N` 必须提供显式、逐版本、可测试的 migration edge。允许内部保留更长链，但不得因此承诺无限期兼容。
- 低于声明窗口的版本返回 `UnknownVersion`，并在 Diagnostic 中携带实际版本和最低支持版本；禁止猜测字段、跳过版本或选择“最近版本”。
- 高于当前版本的存档一律拒绝；不支持 downgrade。
- 删除旧 migration 前必须先更新格式文档、fixture 与 release note，并确保受支持窗口内不存在仍依赖该 edge 的版本。
- migration 必须先在 detached owning value 上完成全部转换与目标 schema/codec 校验，再一次性提交；任一步失败不得改变运行时状态。

## Schema 所有权

Schema 模块只提供注册、解析、校验、迁移、文档和 binding contract 生成能力，
不内置 snapshot、game_event、definitions 或玩法领域的具体 schema，也不会在
模块构造时隐式注册领域格式。

具体 schema 必须由数据格式的权威拥有模块定义和注册，并在真实的导入、反序列化
或跨进程边界执行校验。只被测试注册、但没有生产数据路径消费的 schema 不得加入；
这可以避免 schema 模块成为领域协议的第二真源，也落实“设计出来的系统必须在恰当
位置实际使用”的原则。

## Generated outputs

`SchemaRegistry::generateDocumentation(id, version)` 生成稳定 Markdown；
`generateBindingContract(id, version)` 生成稳定 canonical JSON。两者都只读取
已注册 schema，不执行 migration，不修改 registry。生成结果可以由编辑器、
Squirrel binding 工具或 CI 作为输入，避免手写第二份字段 allowlist。

当前仓库的脚本 binding gap 仍由 `scripts/check_bindings_gaps.txt` 保持兼容；
`scripts/check_binding_gap_metadata.json` 是只读元数据侧车，记录统一 owner、
issue、原因、expiry 和 baseline 数量。`scripts/check_binding_gap_metadata.py`
只校验侧车与现有 gap 文件，不调用 `check_bindings.py --write-gaps`，也不覆盖
现有 lint 的职责。
