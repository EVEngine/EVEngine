#include "common/ScriptComponentSnapshot.h"

#include "common/Diagnostic.h"
#include "common/ECS.h"
#include "common/ScriptComponentStore.h"
#include "common/Value.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace eve {
namespace {

/** payload 里的字段名。集中在这里，免得解析与生成两处各写一遍字符串。 */
constexpr const char* kComponentsField = "components";
constexpr const char* kEntityField     = "entity";
constexpr const char* kNameField       = "name";
constexpr const char* kVersionField    = "version";
constexpr const char* kFieldsField     = "fields";

constexpr std::uint64_t kSchemaVersion = 1;

Result<void> snapshotFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "ecs.snapshot"));
}

/** `<u64>` → 十进制字符串；JSON 数字装不下 u64，所以实体身份一律走字符串。 */
std::string entityIdText(std::uint64_t entityId) { return std::to_string(entityId); }

Result<std::uint64_t> parseEntityId(const Value& value, std::size_t index) {
    if (!value.isString())
        return Result<std::uint64_t>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                "script component snapshot entity id must be a decimal string",
                                                                std::to_string(index), {}, "ecs.snapshot"));
    const std::string& text = value.asString();
    if (text.empty()) return Result<std::uint64_t>::failure(Diagnostic::error(
                          DiagnosticCode::ParseError, "script component snapshot entity id must not be empty",
                          std::to_string(index), {}, "ecs.snapshot"));
    std::uint64_t parsed = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9')
            return Result<std::uint64_t>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "script component snapshot entity id must be decimal digits only", text, {},
                "ecs.snapshot"));
        const std::uint64_t next = parsed * 10u + static_cast<std::uint64_t>(digit - '0');
        if (next < parsed)
            return Result<std::uint64_t>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                    "script component snapshot entity id overflows",
                                                                    text, {}, "ecs.snapshot"));
        parsed = next;
    }
    return Result<std::uint64_t>::success(parsed);
}

// 字段顺序不需要在这里做什么：`Value::Object` 是 `std::map`，序列化与哈希输入永远是字典序（跨进程
// 稳定）。§9.63 那条"按声明顺序遍历"只对**直接遍历 `Value`** 的消费者（例如将来的编辑器列表）有意义，
// 对 JSON/哈希不可观测——所以这里原样搬存储里那一份，既不排序也不丢字段。

}  // namespace

LogicalId scriptComponentSnapshotSchema() {
    // `LogicalId` 的构造是私有的：只能经语法校验的解析入口拿到（命名空间必须小写开头）。
    static const LogicalId schema = *LogicalId::parse("eve:script-components");
    return schema;
}

Result<SnapshotEnvelope> snapshotScriptComponents(PersistentId instanceId, Revision revision, SimulationTick tick,
                                                  const SnapshotHashProvider& hashProvider) {
    Value::Array components;
    for (const ScriptComponentStoreEntry& entry : scriptComponentStoreEntries()) {
        Value::Object item;
        item.emplace(kEntityField, Value::string(entityIdText(entry.key.entityId)));
        item.emplace(kNameField, Value::string(entry.key.name));
        item.emplace(kVersionField, Value::integer(static_cast<std::int64_t>(entry.key.version.value())));
        item.emplace(kFieldsField, entry.values);
        components.push_back(Value::object(std::move(item)));
    }

    Value::Object payload;
    payload.emplace(kComponentsField, Value::array(std::move(components)));
    return makeSnapshotEnvelope(kScriptComponentSnapshotType, scriptComponentSnapshotSchema(),
                                SchemaVersion(kSchemaVersion), instanceId, revision, tick,
                                Value::object(std::move(payload)), hashProvider);
}

Result<void> restoreScriptComponents(const SnapshotEnvelope&     snapshot,
                                     const SnapshotHashProvider& hashProvider) {
    if (snapshot.type != kScriptComponentSnapshotType || snapshot.schema != scriptComponentSnapshotSchema())
        return snapshotFailure(DiagnosticCode::InvalidArgument,
                               "snapshot does not belong to the script component store");
    if (snapshot.schemaVersion.value() != kSchemaVersion)
        return snapshotFailure(DiagnosticCode::UnknownVersion,
                               "script component snapshot version is not the current one", snapshot.schema.format());
    auto verified = verifySnapshotEnvelope(snapshot, hashProvider);
    if (!verified.ok()) return verified;

    const Value* components = snapshot.payload.find(kComponentsField);
    if (components == nullptr || !components->isArray())
        return snapshotFailure(DiagnosticCode::ParseError, "script component snapshot has no components array");

    // 先全部校验进候选表，**不碰**现有数据：恢复失败必须什么都不改（仓库规则：restore 不许留下
    // 部分变更的可观测状态）。
    std::map<ScriptComponentStoreKey, Value> candidate;
    for (std::size_t index = 0; index < components->arraySize(); ++index) {
        const Value& item = components->at(index);
        if (!item.isObject())
            return snapshotFailure(DiagnosticCode::ParseError, "script component snapshot entry must be an object",
                                   std::to_string(index));
        const Value* entity  = item.find(kEntityField);
        const Value* name    = item.find(kNameField);
        const Value* version = item.find(kVersionField);
        const Value* fields  = item.find(kFieldsField);
        if (entity == nullptr || name == nullptr || version == nullptr || fields == nullptr)
            return snapshotFailure(DiagnosticCode::ParseError,
                                   "script component snapshot entry is missing a required field",
                                   std::to_string(index));
        if (!name->isString() || name->asString().empty())
            return snapshotFailure(DiagnosticCode::ParseError, "script component snapshot entry has an empty name",
                                   std::to_string(index));
        if (!version->isInt64() || version->asInt() <= 0)
            return snapshotFailure(DiagnosticCode::ParseError,
                                   "script component snapshot entry has an invalid version", name->asString());
        if (!fields->isObject())
            return snapshotFailure(DiagnosticCode::ParseError,
                                   "script component snapshot entry fields must be an object", name->asString());

        auto entityId = parseEntityId(*entity, index);
        if (!entityId.ok()) return Result<void>::failure(entityId.status());

        const std::string  componentName    = name->asString();
        const SchemaVersion componentVersion(static_cast<std::uint64_t>(version->asInt()));
        const ScriptComponentRecord* record = findScriptComponent(componentName, componentVersion);
        if (record == nullptr)
            return snapshotFailure(DiagnosticCode::NotFound,
                                   "snapshot describes a script component this build does not know",
                                   componentName + "@" + std::to_string(componentVersion.value()));
        // 未知字段策略：显式拒绝。字段集变过的旧载荷必须走迁移链，不能被静默收下——否则存储里会
        // 出现没人声明的字段，序列化/编辑器都会看见它。
        for (const std::string& key : fields->keys()) {
            bool declared = false;
            for (const std::string& field : record->fields) {
                if (field == key) {
                    declared = true;
                    break;
                }
            }
            if (!declared)
                return snapshotFailure(DiagnosticCode::Unsupported,
                                       "script component snapshot field is not in the current schema",
                                       componentName + "." + key);
        }

        const ScriptComponentStoreKey key{entityId.value(), componentName, componentVersion};
        if (candidate.find(key) != candidate.end())
            return snapshotFailure(DiagnosticCode::Conflict, "script component snapshot has a duplicate entry",
                                   componentName);
        candidate.emplace(key, *fields);
    }

    std::vector<ScriptComponentStoreEntry> entries;
    entries.reserve(candidate.size());
    for (auto& [key, values] : candidate) entries.push_back(ScriptComponentStoreEntry{key, std::move(values)});
    replaceScriptComponentValues(std::move(entries));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::string> serializeScriptComponentSnapshot(PersistentId instanceId, Revision revision, SimulationTick tick,
                                                     const SnapshotHashProvider& hashProvider) {
    auto envelope = snapshotScriptComponents(instanceId, revision, tick, hashProvider);
    if (!envelope.ok()) return Result<std::string>::failure(envelope.status());
    return std::move(envelope).andThen(
        [](SnapshotEnvelope&& value) { return serializeSnapshotEnvelope(value); });
}

Result<void> restoreScriptComponentSnapshotJson(std::string_view            json,
                                                const SnapshotHashProvider& hashProvider) {
    auto parsed = parseSnapshotEnvelope(json, hashProvider);
    if (!parsed.ok()) return Result<void>::failure(parsed.status());
    return restoreScriptComponents(parsed.value(), hashProvider);
}

Result<std::vector<ScriptComponentInstanceState>> scriptComponentInstancesOf(const ssq::Object& entityInstance) {
    if (entityInstance.getType() != ssq::Type::INSTANCE)
        return Result<std::vector<ScriptComponentInstanceState>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "script component inspection needs an entity instance",
                              {}, {}, "ecs.snapshot"));
    HSQUIRRELVM vm = entityInstance.getHandle();
    if (vm == nullptr)
        return Result<std::vector<ScriptComponentInstanceState>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "script component inspection needs a live script VM", {},
                              {}, "ecs.snapshot"));

    // 实例上的 `_eid` 是实体身份，`_comps`（都是 `eve.Entity` 的声明槽位）是"这个实体有哪些
    // 组件"的事实来源；每个组件实例上的 `_componentName`/`_componentVersion` 是它登记时的身份
    // （§9.64 的控制槽位）。两步都用公开 sq API 读取，不碰 Squirrel 内部结构。
    std::vector<std::pair<std::string, SchemaVersion>> identities;
    std::uint64_t                                      entityId = 0;
    const SQInteger                                    top      = sq_gettop(vm);
    sq_pushobject(vm, entityInstance.getRaw());

    sq_pushstring(vm, "_eid", -1);
    if (SQ_SUCCEEDED(sq_get(vm, -2))) {
        SQInteger raw = 0;
        if (SQ_SUCCEEDED(sq_getinteger(vm, -1, &raw)) && raw > 0) entityId = static_cast<std::uint64_t>(raw);
    }
    sq_settop(vm, top);

    sq_pushobject(vm, entityInstance.getRaw());
    sq_pushstring(vm, "_comps", -1);
    if (SQ_FAILED(sq_get(vm, -2)) || sq_gettype(vm, -1) != OT_TABLE) {
        sq_settop(vm, top);
        return Result<std::vector<ScriptComponentInstanceState>>::success({});
    }
    sq_pushnull(vm);  // 迭代游标
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        // 栈：instance, comps, cursor, key, value
        if (sq_gettype(vm, -1) == OT_INSTANCE) {
            std::string  name;
            std::int64_t version           = 0;
            const auto   readComponentSlot = [&](const char* slot, bool asText) -> bool {
                const SQInteger inner = sq_gettop(vm);
                sq_push(vm, -1);  // 组件实例
                sq_pushstring(vm, slot, -1);
                const bool found = SQ_SUCCEEDED(sq_get(vm, -2));
                bool       ok    = false;
                if (found && asText) {
                    const SQChar* raw = nullptr;
                    ok                = SQ_SUCCEEDED(sq_getstring(vm, -1, &raw)) && raw != nullptr;
                    if (ok) name = raw;
                } else if (found) {
                    SQInteger raw = 0;
                    ok            = SQ_SUCCEEDED(sq_getinteger(vm, -1, &raw));
                    if (ok) version = static_cast<std::int64_t>(raw);
                }
                sq_settop(vm, inner);
                return ok;
            };
            if (readComponentSlot("_componentName", true) && !name.empty() &&
                readComponentSlot("_componentVersion", false) && version > 0)
                identities.emplace_back(name, SchemaVersion(static_cast<std::uint64_t>(version)));
        }
        sq_pop(vm, 2);  // key, value
    }
    sq_settop(vm, top);

    std::sort(identities.begin(), identities.end());
    identities.erase(std::unique(identities.begin(), identities.end()), identities.end());

    std::vector<ScriptComponentInstanceState> out;
    out.reserve(identities.size());
    for (const auto& [name, version] : identities) {
        ScriptComponentInstanceState state;
        state.name                          = name;
        state.version                       = version;
        const ScriptComponentRecord* record = findScriptComponent(name, version);
        state.generation                    = record != nullptr ? record->generation : Generation(0);

        const Value* stored = findScriptComponentValues(ScriptComponentStoreKey{entityId, name, version});
        // 按声明的顺序收集（inspector 需要稳定顺序）；存储里缺的字段给 null，让"有组件但缺数据"
        // 在工具里可见。
        const std::vector<std::string> fields = record != nullptr ? record->fields : std::vector<std::string>{};
        for (const std::string& field : fields) {
            const Value* value = stored != nullptr ? stored->find(field) : nullptr;
            state.fields.emplace(field, value != nullptr ? *value : Value::null());
        }
        if (record == nullptr && stored != nullptr) {
            if (const auto* object = stored->getIf<Value::Object>()) state.fields = *object;
        }
        out.push_back(std::move(state));
    }
    return Result<std::vector<ScriptComponentInstanceState>>::success(std::move(out));
}

}  // namespace eve
