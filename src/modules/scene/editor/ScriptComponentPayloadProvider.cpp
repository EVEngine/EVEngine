#include "scene/editor/ScriptComponentPayloadProvider.h"

#include "common/ECS.h"
#include "common/ScriptComponentSnapshot.h"
#include "common/ScriptComponentStore.h"
#include "scene/Scene.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::scene_editor {
namespace {

using scene_editing::DiagnosticSeverity;
using scene_editing::DomainOperation;
using scene_editing::EditorStatus;
using scene_editing::EditorValue;
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

/** 域操作类型：一次脚本组件字段的绝对赋值（写入与撤销都用这一个类型）。 */
constexpr char kSetOperationType[] = "eve.script-component.field.set.v1";

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
    if (versionPos == std::string_view::npos || versionPos <= nodeEnd + 1 || versionPos + 1 >= text.size())
        return false;
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

/** 字段类型由声明默认值推断；声明值是 `null` 时退回当前值。`null` 表示两边都没有类型。 */
PropertyType propertyTypeOf(const Value& value) {
    switch (value.type()) {
        case Value::Type::Bool: return PropertyType::Bool;
        case Value::Type::Int64: return PropertyType::Int;
        case Value::Type::Double: return PropertyType::Float;
        case Value::Type::String: return PropertyType::String;
        case Value::Type::Array: return PropertyType::Array;
        case Value::Type::Object: return PropertyType::Struct;
        case Value::Type::Null: break;
    }
    return PropertyType::ReadOnlyText;
}

/** 从域操作载荷里读一个字段（载荷必须是一个对象，否则当成畸形载荷）。 */
const EditorValue* payloadField(const EditorValue& payload, const char* name) {
    const auto* object = payload.getIf<EditorValue::Object>();
    if (object == nullptr) return nullptr;
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

/** 读一个正整数域（撤销重放时同一份载荷可能以整数或浮点回来）。 */
bool readPositiveNumber(const EditorValue& value, std::uint64_t& out) {
    if (const auto* integer = value.getIf<std::int64_t>()) {
        if (*integer <= 0) return false;
        out = static_cast<std::uint64_t>(*integer);
        return true;
    }
    if (const auto* number = value.getIf<double>()) {
        if (!(*number > 0.0)) return false;
        out = static_cast<std::uint64_t>(*number);
        return true;
    }
    return false;
}

template <class T>
Result<T> editRefusal(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

}  // namespace

std::string_view ScriptComponentOperationTarget::setOperationType() { return kSetOperationType; }

void ScriptComponentOperationTarget::bind(const TargetId& target, std::string nodeId, std::uint64_t entityId,
                                          std::string name, eve::SchemaVersion version, eve::Generation generation) {
    target_     = target;
    nodeId_     = std::move(nodeId);
    entityId_   = entityId;
    name_       = std::move(name);
    version_    = version;
    generation_ = generation;
    staging_    = false;
    bound_      = true;

    const Value* stored = eve::findScriptComponentValues(ScriptComponentStoreKey{entityId_, name_, version_});
    fields_             = stored != nullptr && stored->isObject() ? *stored->getIf<Value::Object>() : Value::Object{};
}

std::uint64_t ScriptComponentOperationTarget::revision() const {
    // 脚本组件的字段值不在场景文档里，文档版本号对它没有意义：用引擎侧存储的写版本号，
    // 脚本写、原生写、恢复快照都会让它变。
    return static_cast<std::uint64_t>(eve::scriptComponentStoreRevision());
}

Result<void> ScriptComponentOperationTarget::writeField(const std::string& field, const Value& value) {
    if (staging_) {
        // 候选副本只改自己那份字段：撤销要能"先在候选上试算"，失败不留半套状态。
        const auto found = fields_.find(field);
        if (found == fields_.end())
            return editRefusal<void>(EditorStatus::NotFound, "editor.scene.script-component.candidate-field",
                                     "staged candidate has no declared field: " + field);
        found->second = value;
        return eve::editing::applied<void>();
    }

    const auto written =
        eve::setScriptComponentField(ScriptComponentStoreKey{entityId_, name_, version_}, field, value);
    if (!written.ok())
        return editRefusal<void>(EditorStatus::NotFound, "editor.scene.script-component.storage",
                                 "engine-side script component data is gone or has no such field: " + field);
    return eve::editing::applied<void>();
}

Result<void> ScriptComponentOperationTarget::applyDomainOperation(const DomainOperation& operation) {
    if (!bound_)
        return editRefusal<void>(EditorStatus::Rejected, "editor.scene.script-component.unbound",
                                 "script component operation target is not bound to a component");
    if (std::string_view(operation.type) != setOperationType() || operation.target != target_)
        return editRefusal<void>(EditorStatus::Rejected, "editor.scene.script-component.operation",
                                 "operation does not write a field of this script component target");

    const EditorValue* node           = payloadField(operation.payload, "node");
    const EditorValue* name           = payloadField(operation.payload, "name");
    const EditorValue* version        = payloadField(operation.payload, "version");
    const EditorValue* field          = payloadField(operation.payload, "field");
    const EditorValue* value          = payloadField(operation.payload, "value");
    const auto*        nodeText       = node != nullptr ? node->getIf<std::string>() : nullptr;
    const auto*        nameText       = name != nullptr ? name->getIf<std::string>() : nullptr;
    const auto*        fieldText      = field != nullptr ? field->getIf<std::string>() : nullptr;
    std::uint64_t      payloadVersion = 0;
    if (nodeText == nullptr || nameText == nullptr || fieldText == nullptr || value == nullptr || version == nullptr ||
        !readPositiveNumber(*version, payloadVersion))
        return editRefusal<void>(EditorStatus::Rejected, "editor.scene.script-component.payload",
                                 "script component operation payload is malformed");

    // 载荷自带身份：绑定被后来的选中项改写过的过期目标只会被拒绝，不会写到别的组件上。
    if (*nodeText != nodeId_ || *nameText != name_ || payloadVersion != version_.value())
        return editRefusal<void>(EditorStatus::Conflict, "editor.scene.script-component.stale-target",
                                 "operation belongs to another script component than the bound one");
    return writeField(*fieldText, eve::editing::toPresentationValue(*value));
}

std::unique_ptr<scene_editing::IDomainOperationTarget> ScriptComponentOperationTarget::cloneDomainState() const {
    auto candidate      = std::make_unique<ScriptComponentOperationTarget>(*this);
    candidate->staging_ = true;
    // 候选从**权威状态**重新克隆字段：撤销/重做要整份发布候选，所以它必须握有一份完整字段，
    // 而不是绑定当时那份可能已经过期的拷贝。数据已经不在了就留着绑定时的拷贝，发布时会被拒绝。
    const Value* stored = eve::findScriptComponentValues(ScriptComponentStoreKey{entityId_, name_, version_});
    if (stored != nullptr && stored->isObject()) candidate->fields_ = *stored->getIf<Value::Object>();
    return candidate;
}

Result<void> ScriptComponentOperationTarget::commitDomainState(
    std::unique_ptr<scene_editing::IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<ScriptComponentOperationTarget*>(candidate.get());
    if (typed == nullptr || !typed->staging_ || !bound_ || !typed->bound_ || typed->target_ != target_ ||
        typed->entityId_ != entityId_ || typed->name_ != name_ || typed->version_ != version_)
        return editRefusal<void>(EditorStatus::Conflict, "editor.scene.script-component.candidate",
                                 "staged candidate belongs to another script component");
    if (eve::findScriptComponentValues(ScriptComponentStoreKey{entityId_, name_, version_}) == nullptr)
        return editRefusal<void>(EditorStatus::Conflict, "editor.scene.script-component.missing-data",
                                 "script component data is gone, so the edit cannot be published");
    // 换代（同名同版本重新登记）之后这份数据属于旧 schema：宁可拒绝，也不按新 schema 发布。
    const auto declared = eve::scriptComponentDeclaredSchema(name_, version_);
    if (!declared.ok() || declared.value().generation != generation_)
        return editRefusal<void>(EditorStatus::Conflict, "editor.scene.script-component.stale-generation",
                                 "script component was re-registered since the reference, so the edit is refused");

    // 整份发布：候选是按权威状态克隆的，事务两侧又都有版本号校验，所以这一次
    // `setScriptComponentValues` 就是"原子发布"——不存在写了一半的字段表。
    const auto published = eve::setScriptComponentValues(ScriptComponentStoreKey{entityId_, name_, version_},
                                                         Value::object(typed->fields_));
    if (!published.ok())
        return editRefusal<void>(EditorStatus::Failed, "editor.scene.script-component.publish",
                                 "engine-side store refused the staged script component candidate");
    fields_ = typed->fields_;
    return eve::editing::applied<void>();
}

const std::string& ScriptComponentPayloadProvider::payloadType() {
    static const std::string type = "eve.script-component";
    return type;
}

ScriptComponentPayloadProvider::ScriptComponentPayloadProvider(HostResolver resolver)
    : resolver_(std::move(resolver)) {}

std::string ScriptComponentPayloadProvider::hostNameOf(const scene_editing::TargetId& target) const {
    if (resolver_) {
        std::string resolved = resolver_(target);
        if (!resolved.empty()) return resolved;
    }
    return target.value();
}

const std::string& ScriptComponentPayloadProvider::componentType() const { return payloadType(); }

std::vector<SceneComponentPayloadRef> ScriptComponentPayloadProvider::components(const TargetId& scene,
                                                                                 const ObjectId& object) const {
    std::vector<SceneComponentPayloadRef> out;
    if (scene.empty() || object.empty()) return out;
    scene::Scene* sceneModule = scene::Scene::create();
    if (sceneModule == nullptr) return out;

    const std::string host = hostNameOf(scene);
    for (const ssq::Object& instance : sceneModule->scriptEntitiesAt(host, object.value())) {
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

Result<scene_editing::IDomainOperationTarget*> ScriptComponentPayloadProvider::payloadOperationTarget(
    const SelectionSnapshot& selection) const {
    auto resolved = resolve(selection);
    if (!resolved.ok()) return Result<scene_editing::IDomainOperationTarget*>::failure(resolved.status());
    const Resolved& target = resolved.value();
    operationTarget_.bind(target.target, target.nodeId, target.state.entityId, target.state.name, target.state.version,
                          target.state.generation);
    return eve::editing::applied<scene_editing::IDomainOperationTarget*>(&operationTarget_);
}

std::vector<scene_editing::EditorDiagnostic> ScriptComponentPayloadProvider::validateComponent(
    const SceneComponentPayloadRef& component) const {
    std::string   nodeId;
    std::string   name;
    std::uint64_t version = 0;
    if (component.type != payloadType() || !decodeComponentId(component.component.value(), nodeId, name, version))
        return {eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument,
                                             RuleId("editor.scene.script-component.reference"),
                                             DiagnosticSeverity::Error, "Script component reference is malformed")};

    scene::Scene* sceneModule = scene::Scene::create();
    if (sceneModule == nullptr)
        return {eve::editing::ruleDiagnostic(eve::DiagnosticCode::Failed,
                                             RuleId("editor.scene.script-component.no-scene"),
                                             DiagnosticSeverity::Error, "Scene module is not available")};

    for (const ssq::Object& instance : sceneModule->scriptEntitiesAt(hostNameOf(component.target), nodeId)) {
        auto states = scriptComponentInstancesOf(instance);
        if (!states.ok()) continue;
        for (const ScriptComponentInstanceState& state : states.value()) {
            if (state.name != name || state.version.value() != version) continue;
            if (state.generation.value() != component.generation)
                return {eve::editing::ruleDiagnostic(
                    eve::DiagnosticCode::Conflict, RuleId("editor.scene.script-component.stale"),
                    DiagnosticSeverity::Error, "Script component generation changed since the reference")};
            return {};
        }
    }
    return {eve::editing::ruleDiagnostic(eve::DiagnosticCode::NotFound, RuleId("editor.scene.script-component.missing"),
                                         DiagnosticSeverity::Error, "Script component is no longer on this node")};
}

eve::Result<eve::Revision> ScriptComponentPayloadProvider::currentRevision(const SelectionSnapshot&) const {
    // 脚本组件的字段不在场景文档里，文档版本号对它没有意义；用引擎侧存储的写版本号，
    // 任何一次写（脚本、原生、恢复）都会让它变，inspector 据此刷新。
    return eve::Result<eve::Revision>::success(eve::Revision(scriptComponentStoreRevision()));
}

PropertySchema ScriptComponentPayloadProvider::schemaOf(const ScriptComponentInstanceState& state) const {
    PropertySchema out;
    out.typeId = payloadType();

    const auto declared = eve::scriptComponentDeclaredSchema(state.name, state.version);
    // 声明 schema 拿不到（例如登记已经丢了）时，退回当前值那张表：工具至少看得见"有组件、有这些
    // 数据"，而不是一个空面板；这些字段全部只读（没有声明就没有类型可校验）。
    std::vector<std::string>        fallback;
    const std::vector<std::string>* fields = nullptr;
    if (declared.ok() && !declared.value().fields.empty()) {
        fields = &declared.value().fields;
    } else {
        fallback.reserve(state.fields.size());
        for (const auto& [field, value] : state.fields) fallback.push_back(field);
        fields = &fallback;
    }

    for (std::size_t index = 0; index < fields->size(); ++index) {
        const std::string& field = (*fields)[index];
        const Value*       declaredDefault =
            declared.ok() && index < declared.value().defaults.size() ? &declared.value().defaults[index] : nullptr;
        const auto current    = state.fields.find(field);
        const bool hasCurrent = current != state.fields.end();

        // 可写 = 脚本里给了非 null 的默认值：那才是类型与默认值的来源。声明成 null 的字段只读，
        // 但当前值照样显示（类型从当前值取）。
        const bool   writable   = declaredDefault != nullptr && declaredDefault->type() != Value::Type::Null;
        const Value* typeSource = writable ? declaredDefault : (hasCurrent ? &current->second : nullptr);

        PropertyDescriptor descriptor;
        descriptor.path           = PropertyPath(field);
        descriptor.displayNameKey = field;
        descriptor.category       = state.name;
        descriptor.type           = typeSource != nullptr ? propertyTypeOf(*typeSource) : PropertyType::ReadOnlyText;
        descriptor.defaultValue   = eve::editing::toEditingValue(
            declaredDefault != nullptr ? *declaredDefault : (hasCurrent ? current->second : Value::null()));
        if (!writable) descriptor.flags = PropertyFlag::ReadOnly;
        out.properties.push_back(std::move(descriptor));
    }
    return out;
}

PropertySchema ScriptComponentPayloadProvider::schema(const SelectionSnapshot& selection) const {
    PropertySchema out;
    out.typeId    = payloadType();
    auto resolved = resolve(selection);
    if (!resolved.ok()) return out;
    return schemaOf(resolved.value().state);
}

PropertyReadResult ScriptComponentPayloadProvider::read(const SelectionSnapshot& selection,
                                                        const PropertyPath&      path) const {
    auto resolved = resolve(selection);
    if (!resolved.ok()) return {PropertyReadState::Missing, {}, {}};
    const auto found = resolved.value().state.fields.find(path.value());
    if (found == resolved.value().state.fields.end()) return {PropertyReadState::Missing, {}, {}};
    return {PropertyReadState::Value, eve::editing::toEditingValue(found->second), {}};
}

Result<DomainOperation> ScriptComponentPayloadProvider::planWrite(const Resolved& target, const PropertyPath& path,
                                                                  const EditorValue& value) const {
    const auto        current = target.state.fields.find(path.value());
    const EditorValue previous =
        current != target.state.fields.end() ? eve::editing::toEditingValue(current->second) : EditorValue{};

    DomainOperation operation;
    operation.type        = std::string(ScriptComponentOperationTarget::setOperationType());
    operation.inverseType = operation.type;
    operation.target      = target.target;
    // 载荷里带着组件身份：过期的操作目标据此拒绝写错组件；撤销用的反向值也一起进去。
    operation.payload = EditorValue::Object{
        {"node", EditorValue(target.nodeId)},
        {"name", EditorValue(target.state.name)},
        {"version", EditorValue(static_cast<std::int64_t>(target.state.version.value()))},
        {"field", EditorValue(path.value())},
        {"value", value},
    };
    operation.inverse = EditorValue::Object{
        {"node", EditorValue(target.nodeId)},
        {"name", EditorValue(target.state.name)},
        {"version", EditorValue(static_cast<std::int64_t>(target.state.version.value()))},
        {"field", EditorValue(path.value())},
        {"value", previous},
    };
    operation.hasInverse = true;
    operation.affectedObjects.push_back({target.target, target.nodeId, target.state.generation.value()});
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey = "script-component:" + target.nodeId + ":" + target.state.name + "@" +
                         std::to_string(target.state.version.value()) + ":" + path.value();
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

Result<DomainOperation> ScriptComponentPayloadProvider::makeSet(const SelectionSnapshot& selection,
                                                                const PropertyPath& path, const EditorValue& value,
                                                                PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    if (mode != PropertySetMode::Absolute)
        return editRefusal<DomainOperation>(EditorStatus::Rejected, "editor.scene.script-component.mode",
                                            "script component fields accept absolute edits only");

    auto resolved = resolve(selection);
    if (!resolved.ok()) return Result<DomainOperation>::failure(resolved.status());
    const Resolved& target = resolved.value();

    const auto descriptor = schemaOf(target.state).find(path);
    if (!descriptor)
        return editRefusal<DomainOperation>(EditorStatus::NotFound, "editor.scene.script-component.field",
                                            "script component has no declared field: " + path.value());
    if (hasPropertyFlag(descriptor->flags, PropertyFlag::ReadOnly))
        return editRefusal<DomainOperation>(
            EditorStatus::Rejected, "editor.scene.script-component.untyped",
            "field has no declared non-null default, so its type is unknown and it stays read-only: " + path.value());
    const auto validation = validatePropertyValue(*descriptor, value);
    if (!validation.ok()) return Result<DomainOperation>::failure(validation.status());
    return planWrite(target, path, value);
}

Result<DomainOperation> ScriptComponentPayloadProvider::makeReset(const SelectionSnapshot& selection,
                                                                  const PropertyPath&      path) const {
    auto resolved = resolve(selection);
    if (!resolved.ok()) return Result<DomainOperation>::failure(resolved.status());
    const Resolved& target = resolved.value();

    const auto descriptor = schemaOf(target.state).find(path);
    if (!descriptor)
        return editRefusal<DomainOperation>(EditorStatus::NotFound, "editor.scene.script-component.field",
                                            "script component has no declared field: " + path.value());
    if (hasPropertyFlag(descriptor->flags, PropertyFlag::ReadOnly))
        return editRefusal<DomainOperation>(EditorStatus::Rejected, "editor.scene.script-component.untyped",
                                            "field has no declared non-null default to reset to: " + path.value());
    return planWrite(target, path, descriptor->defaultValue);
}

eve::Result<ScriptComponentPayloadProvider::Resolved> ScriptComponentPayloadProvider::resolve(
    const SelectionSnapshot& selection) const {
    const auto failure = [](eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<Resolved>::failure(
            eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "scene.script-component"));
    };

    if (selection.items.size() != 1)
        return failure(eve::DiagnosticCode::InvalidArgument,
                       "script component properties are edited one component at a time", "selection");
    const scene_editing::SelectionItem& item = selection.items.front();
    if (item.type != payloadType())
        return failure(eve::DiagnosticCode::InvalidArgument,
                       "selection does not belong to the script component provider", item.type);

    std::string   nodeId;
    std::string   name;
    std::uint64_t version = 0;
    if (!decodeComponentId(item.item.value(), nodeId, name, version))
        return failure(eve::DiagnosticCode::InvalidArgument, "script component reference is malformed",
                       item.item.value());

    scene::Scene* sceneModule = scene::Scene::create();
    if (sceneModule == nullptr)
        return failure(eve::DiagnosticCode::Failed, "scene module is not available", item.target.value());

    for (const ssq::Object& instance : sceneModule->scriptEntitiesAt(hostNameOf(item.target), nodeId)) {
        auto states = scriptComponentInstancesOf(instance);
        if (!states.ok()) continue;
        for (const ScriptComponentInstanceState& state : states.value()) {
            if (state.name != name || state.version.value() != version) continue;
            return eve::Result<Resolved>::success(Resolved{item.target, nodeId, state});
        }
    }
    return failure(eve::DiagnosticCode::NotFound, "script component is no longer on this node", item.item.value());
}

}  // namespace eve::scene_editor
