# 策略目录：选择元数据，回调由领域拥有

**入口：** `eve.PolicyRegistryModule()`，槽位 `policies`。newRegistry 返回 PolicyRegistry。
目录保存策略名、版本、优先级、启用状态与 kind 等元数据；不序列化脚本闭包，也不偷偷
执行回调。领域拥有回调和执行时机，目录负责发现。

## 任务一：注册并选择策略

初始化阶段执行，项目需 policies。

```squirrel
local registry = policies.newRegistry();
local registered = registry.insert("administration", "governor", 1, 8, true,
                                   "script", "governor.policy", "{\"owner\":\"game-script\"}");
if (!registered.ok) throw registered.status.summary;
local generation = registered.value.generation;
local resolved = registry.resolve("administration", "governor");
if (!resolved.ok) throw resolved.status.summary;
local selected = registry.select("administration");
assert(selected != null && selected.getName() == "governor");
local callback = function(subject) { return subject + ":handled"; };
assert(callback("general-7") == "general-7:handled");
```

示例 callback 是游戏的变量，不是 registry 自动调用。正式代码按选中策略的引用在领域
回调目录中解析；缺失回调应报告失败，不默认执行另一个策略。

## 任务二：恢复策略目录并重接执行器

```squirrel
local snapshot = registry.snapshotJson();
local candidate = policies.newRegistry();
local restored = candidate.restoreJson(snapshot);
if (!restored.ok) throw restored.status.summary;
local handle = candidate.resolveHandle("administration", "governor", generation);
if (!handle.ok) throw handle.status.summary;
assert(handle.value.kind == "script");
```

只恢复元数据，不恢复 callback。热替换后 handle 的 generation 要重新核对，不能将旧
borrowed entry 跨 replace 保留。禁用策略与删除策略是不同操作；查询不到时 UI 显示不可用，
模拟不能静默跳过必需规则。优先级、平局与顺序按目录的选择契约处理，不依赖表遍历顺序。

注册目录在加载/工具阶段操作；热路径缓存经过验证的解析结果，并通过 generation 失效
检测更新。源码：[PolicyRegistry.cpp](../../../src/modules/policyregistry/PolicyRegistry.cpp)，
脚本发现、选择、恢复且不保存回调的证据：
[policyregistry.cpp](../../../test/integration/script-api/script_api_graphics_mcp_tests.cpp)。

## 补充绑定参考

参数与接收者见 [源码生成参考](policyregistry/api-reference.md)，使用前先确认本章的前置条件与生命周期。
