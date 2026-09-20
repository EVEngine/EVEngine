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
