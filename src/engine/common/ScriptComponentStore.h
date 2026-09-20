#pragma once

/** @file ScriptComponentStore.h @brief 脚本组件字段的引擎侧权威存储。 */

#include "common/Result.h"
#include "common/SchemaVersion.h"
#include "common/Value.h"

#include <cstddef>
#include <cstdint>
#include <string>

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
 * @brief 实体销毁时清掉它的全部组件数据。
 * @return 被清掉的条数。
 */
std::size_t removeScriptComponentValues(std::uint64_t entityId);

/** @brief 换 VM 时清空：脚本世界连同它的组件数据一起消失。 */
void clearScriptComponentStore();

/** @brief 当前数据条数（诊断与测试用）。 */
[[nodiscard]] std::size_t scriptComponentStoreSize();

}  // namespace eve
