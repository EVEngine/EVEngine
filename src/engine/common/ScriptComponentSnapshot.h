#pragma once

/**
 * @file ScriptComponentSnapshot.h
 * @brief 脚本组件数据的持久化信封（方案文档实施顺序第 4 步 / J-3 的存档通路）。
 */

#include "common/Export.h"
#include "common/Generation.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/Revision.h"
#include "common/SchemaVersion.h"
#include "common/Snapshot.h"
#include "common/Time.h"
#include "common/Value.h"

#include <string>
#include <string_view>
#include <vector>

namespace ssq {
class Object;
}

namespace eve {

/** @brief 脚本组件快照的稳定类型（`SnapshotEnvelope::type`）。 */
inline constexpr const char* kScriptComponentSnapshotType = "eve.script-components";

/**
 * @brief 一个脚本实体实例上某个组件的**当前状态**（供引擎工具读取）。
 *
 * @remarks 这是 J-3「编辑器/工具读到脚本组件」的读侧原语：字段来自引擎侧那一个权威存储，
 *          不是脚本实例的槽位。**未登记**的组件（纯槽位语义）不出现在这里——它们没有名字与
 *          版本，引擎工具无从寻址（§9.62.1 的成本策略）。
 */
struct EVENGINE_API_FOUNDATION_INLINE ScriptComponentInstanceState {
    /**
     * @brief 脚本 VM 内的实体 id（实例上的 `_eid`）。
     *
     * 这是引擎侧存储的键的第一段，因此也是"按状态找到那份数据"的唯一途径：编辑器要写回字段
     * 必须先有它（`ScriptComponentStoreKey` 就是 (实体, 组件名, 版本)）。
     */
    std::uint64_t entityId = 0;
    /** @brief 组件名（脚本登记时给的）。 */
    std::string name;
    /** @brief 组件版本。 */
    SchemaVersion version;
    /** @brief 注册表里的代际（同名同版本每次重新登记 +1）。 */
    Generation generation;
    /** @brief 字段名 → 当前值，按注册表记录的**声明顺序**收集（ inspector 需要稳定顺序）。 */
    Value::Object fields;
};

/**
 * @brief 读一个脚本实体实例上挂着的全部**已登记**组件的状态。
 *
 * @param entityInstance `eve.Entity` 子类的实例（例如从 `Scene::scriptEntitiesAt` 拿到的）。
 * @return 按 (名字, 版本) 升序的状态数组；实例没有组件时返回空数组。
 *         `InvalidArgument` 表示传进来的不是实例。
 * @remarks 组件的身份来自实例上的控制槽位（`_componentName`/`_componentVersion`，§9.64），
 *          值来自引擎侧存储；两者对不上（例如类登记过但数据条目已被清掉）时仍会列出该组件、
 *          字段值为 `null`——工具应当看得见"有组件但没有数据"，而不是被静默跳过。
 * @lifetime 返回值拥有自己的存储，脚本 VM 销毁后依然可用。
 * @cost 与"实例上的组件数 × 字段数"成正比；供编辑器/工具按需调用，不在逐帧路径上。
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::vector<ScriptComponentInstanceState>> scriptComponentInstancesOf(
    const ssq::Object& entityInstance);

/** @brief 脚本组件快照的 payload schema：`eve:script-components` 版本 1。 */
[[nodiscard]] EVENGINE_API_FOUNDATION LogicalId scriptComponentSnapshotSchema();

/**
 * @brief 把引擎侧的脚本组件数据封成一个快照信封。
 *
 * payload 形状（版本 1，字段全部必需）：
 * ```json
 * {"components":[{"entity":"7","name":"Position","version":1,"fields":{"x":1.5,"y":2.5}}]}
 * ```
 *
 * @param instanceId 这次快照的状态所有者身份；`PersistentId()`（nil）表示匿名/旧式存储。
 * @param revision 采集时的状态版本。
 * @param tick 采集时的模拟 tick。
 * @param hashProvider 注入的摘要实现；不得为空。
 * @return 封闭好的信封，或结构化的失败。
 * @remarks **顺序是确定的**：条目按 `(实体, 组件名, 版本)` 升序；字段落在 `Value::Object`（`std::map`）
 *          里，序列化与哈希输入因此是字典序。同一份状态两次快照得到同一个哈希，也不含墙钟字段。
 * @cost 与存储里的条目数成正比（一次全量拷贝 + 一次 JSON 规范化）；只在存档/编辑器读时调用。
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<SnapshotEnvelope> snapshotScriptComponents(
    PersistentId instanceId, Revision revision, SimulationTick tick, const SnapshotHashProvider& hashProvider);

/**
 * @brief 用快照里的数据**整体替换**当前的脚本组件数据。
 *
 * @param snapshot 待恢复的信封；会被校验哈希（`verifySnapshotEnvelope`）。
 * @param hashProvider 与封闭时同一个摘要实现。
 * @return 成功，或说明原因的失败。
 * @remarks **未知组件/未知字段一律拒绝，且不改动任何现有数据**（对齐"持久化数据必须有未知字段
 *          策略"）：恢复必须能回答"这份数据描述的是哪个已登记的组件"，否则它读不出来。字段被删掉
 *          的旧载荷因此必须走 `SnapshotMigrationChain`，而不是被静默收下。
 * @remarks **实体身份是 VM 内的 `entityId`**：本函数适合同一 VM 内的回滚/重放/编辑器撤销。跨进程
 *          存档要先有稳定的实体身份（阶段 5 的 `RuntimeObjectRegistry` + `ownerEpoch`），那之前
 *          跨会话恢复只能按创建顺序对上号。
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> restoreScriptComponents(const SnapshotEnvelope&     snapshot,
                                                                           const SnapshotHashProvider& hashProvider);

/** @brief Legacy version-1 JSON compatibility writer; new persistence uses serializeScriptComponentSnapshotBinary().
 * @cost 同 `snapshotScriptComponents` + 一次序列化。 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> serializeScriptComponentSnapshot(
    PersistentId instanceId, Revision revision, SimulationTick tick, const SnapshotHashProvider& hashProvider);

/** @brief Legacy version-1 JSON compatibility importer. @return 成功，或解析/校验/未知组件失败。 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> restoreScriptComponentSnapshotJson(
    std::string_view json, const SnapshotHashProvider& hashProvider);

/**
 * @brief Serialize script component identities and open-ended field values in a typed Protobuf snapshot.
 * @param instanceId Owner identity for the shared binary snapshot envelope.
 * @param revision State revision at capture time.
 * @param tick Simulation tick at capture time.
 * @param hashProvider Injected digest provider for the binary envelope.
 * @return Independent binary snapshot, or a structured encoding failure.
 * @cost Linear in stored components and field bytes per snapshot.
 * @thread Call on the script component store owner thread; no callbacks except the hash provider.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> serializeScriptComponentSnapshotBinary(
    PersistentId instanceId, Revision revision, SimulationTick tick, const SnapshotHashProvider& hashProvider);

/**
 * @brief Validate a typed Protobuf snapshot and replace the script component store atomically.
 * @param bytes Binary snapshot produced by serializeScriptComponentSnapshotBinary().
 * @param hashProvider Digest provider used when the snapshot was sealed.
 * @return Success, or a structured failure leaving the store unchanged.
 * @cost Linear in snapshot bytes and the registered component field count per restore.
 * @thread Call on the script component store owner thread; no callbacks except the hash provider.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> restoreScriptComponentSnapshotBinary(
    std::string_view bytes, const SnapshotHashProvider& hashProvider);

/**
 * @brief 引擎侧脚本组件数据的**工具视图**（MCP `eve_script_components` 与诊断用）。
 *
 * @return 确定性 JSON：
 * ```json
 * {"entities":[{"id":"7","components":[{"name":"Position","version":1,"generation":1,"fields":{"x":1.5}}]}]}
 * ```
 *         条目按 `(实体, 组件名, 版本)` 升序，字段是字典序（`Value::Object` 是 `std::map`）。
 *         实体 id 是**十进制字符串**（`u64` 装不进 JSON 数字，与快照同一约定）。
 * @remarks 这是"引擎工具读得到脚本组件状态"的工具面：只报引擎侧那一个权威存储里的数据
 *          （未登记组件不出现）；与**场景节点**的对应关系在场景模块那一侧（§9.72 的原生读路径 +
 *          编辑器 payload provider）。
 * @cost 与存储里的条目数成正比（一次拷贝 + 一次序列化）；工具/诊断用，不在逐帧路径上。
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> scriptComponentStatesJson();

}  // namespace eve
