#include "scene/editor/ScriptComponentPayloadProvider.h"

#include "common/ScriptComponentSnapshot.h"
#include "common/ScriptComponentStore.h"
#include "scene/Scene.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::scene_editor {
namespace {

using scene_editing::DiagnosticSeverity;
using scene_editing::EditorResult;
using scene_editing::EditorStatus;
using scene_editing::ObjectId;
using scene_editing::PropertyDescriptor;
using scene_editing::PropertyFlag;
using scene_editing::PropertyPath;
using scene_editing::PropertyReadResult;
using scene_editing::PropertyReadState;
using scene_editing::PropertySchema;
using scene_editing::PropertySetMode;
using scene_editing::PropertyType;
using scene_editing::RuleId;
using scene_editing::SceneComponentPayloadRef;
using scene_editing::SelectionSnapshot;
using scene_editing::StableId;
using scene_editing::TargetId;

/** 稳定组件 id 的分隔符：`<节点 id>|<组件名>@<版本>`。 */
constexpr char kNodeSeparator    = '|';
constexpr char kVersionSeparator = '@';

/**
 * 稳定组件 id 里为什么带节点：`makeSceneComponentSelection` 生成的 `SelectionItem` 只有
 * `(domain, target, item, type)`——**节点 id 不在里面**（它只在 `SceneComponentPayloadRef` 上）。
 * 于是 `read`/`schema` 只能从 `item` 里恢复节点；把节点编进稳定 id 是唯一不新增字段的做法。
 */
std::string encodeComponentId(std::string_view nodeId, std::string_view name, std::uint64_t version) {
    return std::string(nodeId) + kNodeSeparator + std::string(name) + kVersionSeparator + std::to_string(version);
}

bool decodeComponentId(std::string_view text, std::string& nodeId, std::string& name, std::uint64_t& version) {
    const std::size_t nodeEnd = text.find(kNodeSeparator);
    if (nodeEnd == std::string_view::npos || nodeEnd == 0) return false;
    const std::size_t versionPos = text.rfind(kVersionSeparator);
    if (versionPos == std::string_view::npos || versionPos <= nodeEnd + 1 || versionPos + 1 >= text.size()) return false;
    nodeId = std::string(text.substr(0, nodeEnd));
    name   = std::string(text.substr(nodeEnd + 1, versionPos - nodeEnd - 1));
    if (name.empty()) return false;
    std::uint64_t parsed = 0;
    for (const char digit : text.substr(versionPos + 1)) {
        if (digit < '0' || digit > '9') return false;
        parsed = parsed * 10u + static_cast<std::uint64_t>(digit - '0');
    }
    if (parsed == 0) return false;
    version = parsed;
    return true;
}

/** 字段类型由**当前值**推断：脚本的声明里只有默认值，没有类型（标记 `eve.Number` 已在登记时解析成真值）。 */
PropertyType propertyTypeOf(const Value& value) {
    switch (value.type()) {
        case Value::Type::Bool: return PropertyType::Bool;
        case Value::Type::Int64: return PropertyType::Int;
        case Value::Type::Double: return PropertyType::Float;
        case Value::Type::String: return PropertyType::String;
        case Value::Type::Array: return PropertyType::Array;
        case Value::Type::Object: return PropertyType::Struct;
    }
    return PropertyType::ReadOnlyText;
}

template <class T>
EditorResult<T> readOnlyRefusal(std::string message) {
    return eve::editing::failed<T>(EditorStatus::Unsupported, eve::DiagnosticCode::Unsupported,
                                   RuleId("editor.scene.script-component.read-only"), std::move(message));
}

Result<std::vector<ScriptComponentInstanceState>> resolutionFailure(eve::DiagnosticCode code, std::string message,
                                                                    std::string path) {
    return Result<std::vector<ScriptComponentInstanceState>>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "scene.script-component"));
}

}  // namespace

const std::string& ScriptComponentPayloadProvider::payloadType() {
    static const std::string type = "eve.script-component";
    return type;
}

const std::string& ScriptComponentPayloadProvider::componentType() const { return payloadType(); }

std::vector<SceneComponentPayloadRef> ScriptComponentPayloadProvider::components(const TargetId& scene,
                                                                                 const ObjectId& object) const {
    std::vector<SceneComponentPayloadRef> out;
    if (scene.empty() || object.empty()) return out;
    scene::Scene* sceneModule = scene::Scene::create();
    if (sceneModule == nullptr) return out;

    for (const ssq::Object& instance : sceneModule->scriptEntitiesAt(scene.value(), object.value())) {
        auto states = scriptComponentInstancesOf(instance);
        if (!states.ok()) continue;
        for (const ScriptComponentInstanceState& state : states.value()) {
            SceneComponentPayloadRef ref;
            ref.target     = scene;
            ref.object     = object;
            ref.component  = StableId(encodeComponentId(object.value(), state.name, state.version.value()));
            ref.type       = payloadType();
            ref.generation = state.generation.value();
            ref.revision   = scriptComponentStoreRevision();
            out.push_back(std::move(ref));
        }
    }
    return out;
}

EditorResult<scene_editing::IDomainOperationTarget*> ScriptComponentPayloadProvider::payloadOperationTarget(
    const SelectionSnapshot&) const {
    return readOnlyRefusal<scene_editing::IDomainOperationTarget*>(
        "script component fields are read-only in this build: editing needs a domain operation target with staging");
}

std::vector<scene_editing::EditorDiagnostic> ScriptComponentPayloadProvider::validateComponent(
    const SceneComponentPayloadRef& component) const {
    std::string   nodeId;
    std::string   name;
    std::uint64_t version = 0;
    if (component.type != payloadType() || !decodeComponentId(component.component.value(), nodeId, name, version))
        return {eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument,
                                             RuleId("editor.scene.script-component.reference"),
                                             DiagnosticSeverity::Error,
                                             "Script component reference is malformed")};

    scene::Scene* sceneModule = scene::Scene::create();
    if (sceneModule == nullptr)
        return {eve::editing::ruleDiagnostic(eve::DiagnosticCode::Failed,
                                             RuleId("editor.scene.script-component.no-scene"),
                                             DiagnosticSeverity::Error, "Scene module is not available")};

    for (const ssq::Object& instance : sceneModule->scriptEntitiesAt(component.target.value(), nodeId)) {
        auto states = scriptComponentInstancesOf(instance);
        if (!states.ok()) continue;
        for (const ScriptComponentInstanceState& state : states.value()) {
            if (state.name != name || state.version.value() != version) continue;
            if (state.generation.value() != component.generation)
                return {eve::editing::ruleDiagnostic(eve::DiagnosticCode::Conflict,
                                                     RuleId("editor.scene.script-component.stale"),
                                                     DiagnosticSeverity::Error,
                                                     "Script component generation changed since the reference")};
            return {};
        }
    }
    return {eve::editing::ruleDiagnostic(eve::DiagnosticCode::NotFound,
                                         RuleId("editor.scene.script-component.missing"),
                                         DiagnosticSeverity::Error, "Script component is no longer on this node")};
}

eve::Result<eve::Revision> ScriptComponentPayloadProvider::currentRevision(const SelectionSnapshot&) const {
    // 脚本组件的字段不在场景文档里，文档版本号对它没有意义；用引擎侧存储的写版本号，
    // 任何一次写（脚本、原生、恢复）都会让它变，inspector 据此刷新。
    return eve::Result<eve::Revision>::success(eve::Revision(scriptComponentStoreRevision()));
}

PropertySchema ScriptComponentPayloadProvider::schema(const SelectionSnapshot& selection) const {
    PropertySchema out;
    out.typeId = payloadType();
    auto states = resolve(selection);
    if (!states.ok() || states.value().empty()) return out;

    for (const auto& [field, value] : states.value().front().fields) {
        PropertyDescriptor descriptor;
        descriptor.path           = PropertyPath(field);
        descriptor.displayNameKey = field;
        descriptor.category       = states.value().front().name;
        descriptor.type           = propertyTypeOf(value);
        descriptor.defaultValue   = eve::editing::toEditingValue(value);
        descriptor.flags          = PropertyFlag::ReadOnly;
        out.properties.push_back(std::move(descriptor));
    }
    return out;
}

PropertyReadResult ScriptComponentPayloadProvider::read(const SelectionSnapshot& selection,
                                                        const PropertyPath&      path) const {
    auto states = resolve(selection);
    if (!states.ok() || states.value().empty())
        return {PropertyReadState::Missing, {}, {}};
    const auto found = states.value().front().fields.find(path.value());
    if (found == states.value().front().fields.end()) return {PropertyReadState::Missing, {}, {}};
    return {PropertyReadState::Value, eve::editing::toEditingValue(found->second), {}};
}

EditorResult<scene_editing::DomainOperation> ScriptComponentPayloadProvider::makeSet(
    const SelectionSnapshot&, const PropertyPath&, const scene_editing::EditorValue&, PropertySetMode) const {
    return readOnlyRefusal<scene_editing::DomainOperation>(
        "script component fields are read-only in this build; the write path needs a domain operation target");
}

EditorResult<scene_editing::DomainOperation> ScriptComponentPayloadProvider::makeReset(const SelectionSnapshot&,
                                                                                       const PropertyPath&) const {
    return readOnlyRefusal<scene_editing::DomainOperation>(
        "script component fields are read-only in this build; reset needs the declared default from the registry");
}

Result<std::vector<ScriptComponentInstanceState>> ScriptComponentPayloadProvider::resolve(
    const SelectionSnapshot& selection) const {
    if (selection.items.size() != 1)
        return resolutionFailure(eve::DiagnosticCode::InvalidArgument,
                                 "script component properties are read one component at a time", "selection");
    const scene_editing::SelectionItem& item = selection.items.front();
    if (item.type != payloadType())
        return resolutionFailure(eve::DiagnosticCode::InvalidArgument,
                                 "selection does not belong to the script component provider", item.type);

    std::string   nodeId;
    std::string   name;
    std::uint64_t version = 0;
    if (!decodeComponentId(item.item.value(), nodeId, name, version))
        return resolutionFailure(eve::DiagnosticCode::InvalidArgument,
                                 "script component reference is malformed", item.item.value());

    scene::Scene* sceneModule = scene::Scene::create();
    if (sceneModule == nullptr)
        return resolutionFailure(eve::DiagnosticCode::Failed, "scene module is not available", item.target.value());

    for (const ssq::Object& instance : sceneModule->scriptEntitiesAt(item.target.value(), nodeId)) {
        auto states = scriptComponentInstancesOf(instance);
        if (!states.ok()) continue;
        for (const ScriptComponentInstanceState& state : states.value()) {
            if (state.name != name || state.version.value() != version) continue;
            return Result<std::vector<ScriptComponentInstanceState>>::success({state});
        }
    }
    return resolutionFailure(eve::DiagnosticCode::NotFound, "script component is no longer on this node",
                             item.item.value());
}

}  // namespace eve::scene_editor
