#pragma once

/** @file ScriptComponentStore.h @brief 脚本组件字段的引擎侧权威存储。 */

#include "common/Result.h"
#include "common/SchemaVersion.h"
#include "common/Value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eve {

/**
 * @brief 一条脚本组件数据的键：实体 + 组件名 + 版本。
 *
 * 用名字 + 版本而不是类对象：热重载会换掉类对象，而这两者不变（见 `ECS.h` 的
 * `ScriptComponentRecord`）。这两个身份合起来才能回答"这份数据属于哪个组件的哪一代"。
 */
struct ScriptComponentStoreKey {
    std::uint64_t entityId = 0;
    /** @brief 脚本侧声明的组件名。 */
    std::string name;
    /** @brief 脚本侧声明的版本。 */
    SchemaVersion version;

    friend bool operator<(const ScriptComponentStoreKey& a, const ScriptComponentStoreKey& b) noexcept {
        if (a.entityId != b.entityId) return a.entityId < b.entityId;
        if (a.name != b.name) return a.name < b.name;
        return a.version < b.version;
    }
};

/**
 * @brief 写入一条组件的引擎侧数据（创建实体、或迁移到新代时调用）。
 *
 * @param values 必须是 Object（字段名 → 值）；其它类型返回 `InvalidArgument`。
 * @return 成功，或说明原因的失败。
 * @remarks 这是**唯一权威**的脚本组件字段存储：脚本侧的字段读写最终都落到这里
 *          （见 `_get`/`_set` 接缝），原生侧（打包/存档/编辑器）也从这里读。
 * @cost 一次 map 插入 + 一次 `Value` 拷贝。
 */
[[nodiscard]] Result<void> setScriptComponentValues(const ScriptComponentStoreKey& key, Value values);

/**
 * @brief 读一条组件数据。
 * @return 只读指针，或在没有该键时返回 nullptr。
 * @lifetime 在下一次 `set`/`remove`/`clear` 之前有效。
 * @remarks 字段按**名字**查找；需要稳定顺序时（序列化/Git diff）按注册表记录里的**声明顺序**
 *          遍历，而不是遍历这个 map（`Value::Object` 是 `std::map`，顺序是字典序）。
 */
[[nodiscard]] const Value* findScriptComponentValues(const ScriptComponentStoreKey& key);

/**
 * @brief 读一个字段。
 * @return 字段值，或 `NotFound`（键或字段不存在）。
 */
[[nodiscard]] Result<Value> scriptComponentField(const ScriptComponentStoreKey& key, const std::string& field);

/**
 * @brief 写一个字段（脚本侧的写入与原生侧的写入都走这里）。
 * @return 成功，或 `NotFound`（该组件数据不存在）。
 * @remarks **不**接受未在 schema 里的新字段：组件的数据形状由 schema 决定，凭空加字段会让
 *          序列化与编辑器看到一份没有声明的数据。要加字段就改脚本里的组件类并升版本。
 */
[[nodiscard]] Result<void> setScriptComponentField(const ScriptComponentStoreKey& key, const std::string& field,
                                                   Value value);

/**
 * @brief 存储里的一条数据：键 + 一份字段对象。
 * @remarks 存在的意义是**快照/诊断**要能整体枚举存储；日常读写请用下面按键的函数。
 */
struct ScriptComponentStoreEntry {
    ScriptComponentStoreKey key;
    /** @brief 字段名 → 值（必须是 Object）。 */
    Value values;
};

/**
 * @brief 列出存储里的全部条目，按 (实体, 组件名, 版本) 升序。
 * @return 条目的拷贝；顺序由 `ScriptComponentStoreKey::operator<` 决定，因此跨进程稳定。
 * @cost 一次全量深拷贝；只给快照/编辑器/诊断用，不在热路径上。
 */
[[nodiscard]] std::vector<ScriptComponentStoreEntry> scriptComponentStoreEntries();

/**
 * @brief 用给定条目**整体替换**存储（恢复快照用）。
 *
 * @param entries 新内容；调用方负责先校验完（未知组件/未知字段/重复键），因为本函数只做替换。
 * @remarks 全有或全无：先把条目插进一张临时表，再一次性换掉，恢复失败不会留下半套数据。
 */
void replaceScriptComponentValues(std::vector<ScriptComponentStoreEntry> entries);

/**
 * @brief 把某个组件（名字 + 版本）在**所有实体**上的数据重建到新 schema 上（热重载换代）。
 *
 * 两代都有的字段**保留旧值**；新代新增的字段取 `defaults` 里对应的声明默认值；旧代被删掉的
 * 字段丢弃——存储里不留没人声明的字段（否则序列化/编辑器会看到它）。
 *
 * @param name 组件名；@param version 组件版本。
 * @param fields 新 schema 的字段名，声明顺序。
 * @param defaults 与 `fields` 一一对应的声明默认值；缺项按 `null` 处理。
 * @remarks 这是热重载迁移里**唯一需要 C++ 做**的部分：字段数据按版本寻址，所以换代时
 *          必须显式对齐 schema。实例的重接由脚本侧 ECS 负责（它才知道哪些实体活着）。
 * @cost 与"该组件已存在的实体数"成正比；只应在脚本重载时调用，不在任何热路径上。
 */
void reconcileScriptComponentValues(const std::string& name, SchemaVersion version,
                                    const std::vector<std::string>& fields, const std::vector<Value>& defaults);

/**
 * @brief 实体销毁时清掉它的全部组件数据。
 * @return 被清掉的条数。
 */
std::size_t removeScriptComponentValues(std::uint64_t entityId);

/** @brief 换 VM 时清空：脚本世界连同它的组件数据一起消失。 */
void clearScriptComponentStore();

/** @brief 当前数据条数（诊断与测试用）。 */
[[nodiscard]] std::size_t scriptComponentStoreSize();

/**
 * @brief 存储的单调版本号：任何一次写（含恢复、换代对齐、实体销毁）都会 +1。
 *
 * 编辑器/工具用它判断"我手里的引用是否过期"：`IPropertyProvider::currentRevision` 要一个非零
 * 且会变的版本，而脚本组件的字段值不在场景文档里，文档版本号对它没有意义。
 *
 * @return 当前版本号；进程内从 1 开始单调递增，**不会**因为清空而回退。
 * @cost O(1)。
 */
[[nodiscard]] std::uint64_t scriptComponentStoreRevision();

}  // namespace eve
