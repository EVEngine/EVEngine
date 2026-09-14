#include "editor/EditorPluginPermissions.h"

#include <set>
#include <utility>

namespace eve::editor {
namespace {
template <class T>
EditorResult<T> permissionError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}
const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}
EditorValue grantValue(const PluginPermissionGrant& grant) {
    return EditorValue::Object{{"id", grant.id.value()}, {"plugin", grant.plugin}, {"capability", grant.capability},
                               {"scope", grant.scope}, {"decision", grant.decision}};
}
EditorResult<PluginPermissionGrant> parseGrant(const EditorValue& value) {
    auto string = [&](const char* key) -> const std::string* {
        const auto* entry = field(value, key);
        return entry ? entry->getIf<std::string>() : nullptr;
    };
    const auto *id = string("id"), *plugin = string("plugin"), *capability = string("capability"),
               *scope = string("scope"), *decision = string("decision");
    static const std::set<std::string> capabilities{"filesystem.read", "filesystem.write", "network.connect",
                                                     "database.connect", "script.native", "process.execute"};
    static const std::set<std::string> decisions{"allow", "deny", "ask"};
    if (!id || id->empty() || !plugin || plugin->empty() || !capability || !capabilities.contains(*capability) ||
        !scope || scope->empty() || !decision || !decisions.contains(*decision) || *scope == "*" || *scope == "/")
        return permissionError<PluginPermissionGrant>(
            EditorStatus::Rejected, "editor.plugins.invalid-permission",
            "Permission requires plugin, known capability, explicit narrow scope and decision");
    return eve::editing::applied<PluginPermissionGrant>({StableId(*id), *plugin, *capability, *scope, *decision});
}
DomainOperation operation(const char* type, const char* inverseType, const std::string& target, EditorValue payload,
                          EditorValue inverse, const StableId& id) {
    DomainOperation result;
    result.type = type; result.inverseType = inverseType; result.target = TargetId(target);
    result.payload = std::move(payload); result.inverse = std::move(inverse); result.hasInverse = true;
    result.affectedObjects.push_back({TargetId(target), id.value(), 0});
    return result;
}
}  // namespace

PluginPermissionTarget::PluginPermissionTarget(std::string id) : id_(std::move(id)) {}
TargetDescriptor PluginPermissionTarget::describe() const {
    return {TargetId(id_), "plugin-permissions", revision_, false,
            {CapabilityId("eve.editor.target.plugin-permissions")}};
}
void* PluginPermissionTarget::queryCapability(const CapabilityId& capability) {
    return capability == CapabilityId("eve.editor.target.plugin-permissions") ? this : nullptr;
}
EditorResult<void> PluginPermissionTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_))
        return permissionError<void>(EditorStatus::Rejected, "editor.plugins.permission-target",
                                     "Permission operation targets another document");
    if (operation.type == "plugin.permission.set.v1") {
        auto parsed = parseGrant(operation.payload);
        if (!parsed.ok()) return EditorResult<void>::failure(parsed.status());
        for (const auto& [id, current] : grants_)
            if (id != parsed.value().id && current.plugin == parsed.value().plugin &&
                current.capability == parsed.value().capability && current.scope == parsed.value().scope)
                return permissionError<void>(EditorStatus::Conflict, "editor.plugins.duplicate-permission",
                                             "Equivalent permission already exists");
        grants_.insert_or_assign(parsed.value().id, parsed.value());
    } else if (operation.type == "plugin.permission.remove.v1") {
        const auto* id = operation.payload.getIf<std::string>();
        if (!id || !grants_.erase(StableId(*id)))
            return permissionError<void>(EditorStatus::NotFound, "editor.plugins.permission-not-found",
                                         "Permission grant was not found");
    } else {
        return permissionError<void>(EditorStatus::Rejected, "editor.plugins.permission-operation",
                                     "Unsupported permission operation");
    }
    ++revision_; dirty_.include(0, 0);
    return eve::editing::applied<void>();
}
std::unique_ptr<IDomainOperationTarget> PluginPermissionTarget::cloneDomainState() const {
    return std::make_unique<PluginPermissionTarget>(*this);
}
EditorResult<void> PluginPermissionTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<PluginPermissionTarget*>(candidate.get());
    if (!typed || typed->id_ != id_)
        return permissionError<void>(EditorStatus::Rejected, "editor.plugins.permission-staging",
                                     "Invalid staged permission state");
    *this = *typed;
    return eve::editing::applied<void>();
}
std::vector<PluginPermissionGrant> PluginPermissionTarget::grants() const {
    std::vector<PluginPermissionGrant> result;
    for (const auto& [id, grant] : grants_) { (void)id; result.push_back(grant); }
    return result;
}
EditorResult<DomainOperation> PluginPermissionTarget::makeSet(const PluginPermissionGrant& grant) const {
    auto parsed = parseGrant(grantValue(grant));
    if (!parsed.ok()) return EditorResult<DomainOperation>::failure(parsed.status());
    for (const auto& [id, current] : grants_)
        if (id != grant.id && current.plugin == grant.plugin && current.capability == grant.capability &&
            current.scope == grant.scope)
            return permissionError<DomainOperation>(EditorStatus::Conflict, "editor.plugins.duplicate-permission",
                                                    "Equivalent permission already exists");
    const auto found = grants_.find(grant.id);
    return eve::editing::applied<DomainOperation>(operation(
        "plugin.permission.set.v1", found == grants_.end() ? "plugin.permission.remove.v1" : "plugin.permission.set.v1",
        id_, grantValue(grant), found == grants_.end() ? EditorValue(grant.id.value()) : grantValue(found->second),
        grant.id));
}
EditorResult<DomainOperation> PluginPermissionTarget::makeRemove(const StableId& id) const {
    const auto found = grants_.find(id);
    if (found == grants_.end())
        return permissionError<DomainOperation>(EditorStatus::NotFound, "editor.plugins.permission-not-found",
                                                "Permission grant was not found");
    return eve::editing::applied<DomainOperation>(operation("plugin.permission.remove.v1", "plugin.permission.set.v1",
                                                            id_, id.value(), grantValue(found->second), id));
}
std::string PluginPermissionTarget::decision(const std::string& plugin, const std::string& capability,
                                             const std::string& scope) const {
    for (const auto& [id, grant] : grants_) {
        (void)id;
        if (grant.plugin == plugin && grant.capability == capability && grant.scope == scope) return grant.decision;
    }
    return "ask";
}
EditorValue PluginPermissionTarget::snapshotValue() const {
    EditorValue::Array grants;
    for (const auto& [id, grant] : grants_) { (void)id; grants.push_back(grantValue(grant)); }
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"grants", std::move(grants)}};
}
EditorResult<void> PluginPermissionTarget::loadSnapshot(const EditorValue& snapshot) {
    const auto *versionEntry = field(snapshot, "schemaVersion"), *grantsEntry = field(snapshot, "grants");
    const auto* version = versionEntry ? versionEntry->getIf<int64_t>() : nullptr;
    const auto* grants = grantsEntry ? grantsEntry->getIf<EditorValue::Array>() : nullptr;
    if (!version || *version != 1 || !grants)
        return permissionError<void>(EditorStatus::Rejected, "editor.plugins.permission-snapshot",
                                     "Invalid permission snapshot envelope");
    PluginPermissionTarget candidate(id_);
    for (const auto& entry : *grants) {
        auto parsed = parseGrant(entry);
        if (!parsed.ok()) return EditorResult<void>::failure(parsed.status());
        auto planned = candidate.makeSet(parsed.value());
        if (!planned.ok()) return EditorResult<void>::failure(planned.status());
        auto applied = candidate.applyDomainOperation(planned.value());
        if (!applied.ok()) return applied;
    }
    grants_ = std::move(candidate.grants_); ++revision_; dirty_.include(0, 0);
    return eve::editing::applied<void>();
}
}  // namespace eve::editor
