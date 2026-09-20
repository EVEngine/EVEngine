#include "common/ScriptComponentStore.h"

#include "common/Diagnostic.h"

#include <map>
#include <utility>

namespace eve {
namespace {

using Store = std::map<ScriptComponentStoreKey, Value>;

Store& scriptComponentStore() {
    static Store store;
    return store;
}

Diagnostic storeDiagnostic(DiagnosticCode code, std::string message, const std::string& path) {
    return Diagnostic::error(code, std::move(message), path, {}, "ecs.component.store");
}

}  // namespace

Result<void> setScriptComponentValues(const ScriptComponentStoreKey& key, Value values) {
    if (!values.isObject())
        return Result<void>::failure(
            storeDiagnostic(DiagnosticCode::InvalidArgument, "script component values must be an object",
                            key.name));
    scriptComponentStore()[key] = std::move(values);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

const Value* findScriptComponentValues(const ScriptComponentStoreKey& key) {
    const Store& store = scriptComponentStore();
    const auto   found = store.find(key);
    return found == store.end() ? nullptr : &found->second;
}

Result<Value> scriptComponentField(const ScriptComponentStoreKey& key, const std::string& field) {
    const Value* values = findScriptComponentValues(key);
    if (values == nullptr)
        return Result<Value>::failure(
            storeDiagnostic(DiagnosticCode::NotFound, "no stored values for this component generation", key.name));
    const Value* value = values->find(field);
    if (value == nullptr)
        return Result<Value>::failure(
            storeDiagnostic(DiagnosticCode::NotFound, "field is not part of the component schema", field));
    return Result<Value>::success(*value);
}

Result<void> setScriptComponentField(const ScriptComponentStoreKey& key, const std::string& field, Value value) {
    Store&     store = scriptComponentStore();
    const auto found = store.find(key);
    if (found == store.end())
        return Result<void>::failure(
            storeDiagnostic(DiagnosticCode::NotFound, "no stored values for this component generation", key.name));
    if (!found->second.isObject() || found->second.find(field) == nullptr)
        return Result<void>::failure(
            storeDiagnostic(DiagnosticCode::NotFound, "field is not part of the component schema", field));
    *found->second.find(field) = std::move(value);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::vector<ScriptComponentStoreEntry> scriptComponentStoreEntries() {
    const Store&                           store = scriptComponentStore();
    std::vector<ScriptComponentStoreEntry> out;
    out.reserve(store.size());
    for (const auto& [key, values] : store) out.push_back(ScriptComponentStoreEntry{key, values});
    return out;
}

void replaceScriptComponentValues(std::vector<ScriptComponentStoreEntry> entries) {
    Store replacement;
    for (auto& entry : entries) replacement[entry.key] = std::move(entry.values);
    scriptComponentStore() = std::move(replacement);
}

void reconcileScriptComponentValues(const std::string& name, SchemaVersion version,
                                    const std::vector<std::string>& fields, const std::vector<Value>& defaults) {
    Store& store = scriptComponentStore();
    for (auto& [key, values] : store) {
        if (key.name != name || key.version != version) continue;
        // 逐字段重建：旧的留着、新的取默认值、不再声明的丢掉。原地改 value 而不是插删键，
        // 迭代器因此不会失效。
        Value::Object rebuilt;
        for (std::size_t index = 0; index < fields.size(); ++index) {
            const Value* existing = values.isObject() ? values.find(fields[index]) : nullptr;
            if (existing != nullptr) {
                rebuilt.emplace(fields[index], *existing);
            } else {
                rebuilt.emplace(fields[index], index < defaults.size() ? defaults[index] : Value::null());
            }
        }
        values = Value::object(std::move(rebuilt));
    }
}

std::size_t removeScriptComponentValues(std::uint64_t entityId) {
    Store&      store = scriptComponentStore();
    std::size_t removed = 0;
    for (auto it = store.begin(); it != store.end();) {
        if (it->first.entityId == entityId) {
            it = store.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void clearScriptComponentStore() { scriptComponentStore().clear(); }

std::size_t scriptComponentStoreSize() { return scriptComponentStore().size(); }

}  // namespace eve
