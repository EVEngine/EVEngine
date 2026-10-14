#include "scene/editing/SceneComponentPayload.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include "scene/editing/SceneEditingCommands.h"
#include "scene/editing/SceneTarget.h"

// SceneComponentPayload.cpp
namespace eve::scene_editing {
namespace {}  // namespace

SceneComponentPropertyBindings::SceneComponentPropertyBindings(std::string componentType)
    : componentType_(std::move(componentType)) {}

Result<void> SceneComponentPropertyBindings::bind(SceneComponentPayloadRef component, SelectionItem moduleSelection,
                                                  IPropertyProvider* properties, IDomainOperationTarget* operations,
                                                  Validator validator) {
    if (componentType_.empty() || component.target.empty() || component.object.empty() || component.component.empty() ||
        component.type != componentType_ || !properties || !operations ||
        moduleSelection.target != TargetId(operations->targetId()))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.scene.component-binding-invalid"),
                                          "Component binding requires matching stable ids and module targets");
    if (find(component.target, component.component))
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.scene.component-binding-duplicate"),
                                          "Scene component is already bound: " + component.component.value());
    component.revision = operations->revision();
    bindings_.push_back(
        {std::move(component), std::move(moduleSelection), properties, operations, std::move(validator)});
    return eve::editing::applied<void>();
}

SceneComponentChange SceneComponentPropertyBindings::unbind(const TargetId& scene, const StableId& component) {
    const auto found = std::find_if(bindings_.begin(), bindings_.end(), [&](const Binding& binding) {
        return binding.component.target == scene && binding.component.component == component;
    });
    if (found == bindings_.end()) return SceneComponentChange::Unchanged;
    bindings_.erase(found);
    return SceneComponentChange::Changed;
}

const SceneComponentPropertyBindings::Binding* SceneComponentPropertyBindings::find(const TargetId& scene,
                                                                                    const StableId& component) const {
    const auto found = std::find_if(bindings_.begin(), bindings_.end(), [&](const Binding& binding) {
        return binding.component.target == scene && binding.component.component == component;
    });
    return found == bindings_.end() ? nullptr : &*found;
}

std::vector<SceneComponentPayloadRef> SceneComponentPropertyBindings::components(const TargetId& scene,
                                                                                 const ObjectId& object) const {
    std::vector<SceneComponentPayloadRef> result;
    for (const Binding& binding : bindings_) {
        if (binding.component.target != scene || binding.component.object != object) continue;
        SceneComponentPayloadRef component = binding.component;
        component.revision                 = binding.operations->revision();
        result.push_back(std::move(component));
    }
    return result;
}

Result<std::pair<const SceneComponentPropertyBindings::Binding*, SelectionSnapshot>>
SceneComponentPropertyBindings::translate(const SelectionSnapshot& selection) const {
    if (selection.items.empty())
        return eve::editing::failed<std::pair<const Binding*, SelectionSnapshot>>(
            EditorStatus::Rejected, RuleId("editor.scene.component-selection-empty"),
            "Component property selection cannot be empty");
    const Binding* first = find(selection.items.front().target, selection.items.front().item);
    if (!first || selection.items.front().type != componentType_)
        return eve::editing::failed<std::pair<const Binding*, SelectionSnapshot>>(
            EditorStatus::NotFound, RuleId("editor.scene.component-binding-missing"),
            "Selected scene component is not bound to this provider");
    SelectionSnapshot translated;
    translated.channel  = selection.channel;
    translated.sequence = selection.sequence;
    for (const SelectionItem& item : selection.items) {
        const Binding* binding = find(item.target, item.item);
        if (!binding || item.type != componentType_ || binding->properties != first->properties ||
            binding->operations != first->operations)
            return eve::editing::failed<std::pair<const Binding*, SelectionSnapshot>>(
                EditorStatus::Rejected, RuleId("editor.scene.component-binding-mixed-target"),
                "Multi-edit requires components owned by one module property target");
        translated.items.push_back(binding->moduleSelection);
    }
    translated.primary = translated.items.front();
    return eve::editing::applied<std::pair<const Binding*, SelectionSnapshot>>({first, std::move(translated)});
}

Result<IDomainOperationTarget*> SceneComponentPropertyBindings::payloadOperationTarget(
    const SelectionSnapshot& selection) const {
    auto translated = translate(selection);
    if (!translated.ok()) return Result<IDomainOperationTarget*>::failure(translated.status());
    return eve::editing::applied<IDomainOperationTarget*>(translated.value().first->operations);
}

std::vector<EditorDiagnostic> SceneComponentPropertyBindings::validateComponent(
    const SceneComponentPayloadRef& component) const {
    const Binding* binding = find(component.target, component.component);
    if (!binding)
        return {eve::editing::ruleDiagnostic(eve::DiagnosticCode::NotFound,
                                             RuleId("editor.scene.component-binding-missing"),
                                             DiagnosticSeverity::Error, "Scene component binding no longer exists")};
    return binding->validator ? binding->validator() : std::vector<EditorDiagnostic>{};
}

eve::Result<eve::Revision> SceneComponentPropertyBindings::currentRevision(const SelectionSnapshot& selection) const {
    auto translated = translate(selection);
    if (!translated.ok())
        return eve::Result<eve::Revision>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Scene component selection is not bound",
                                   "editor.scene.component-binding", {}, "editor.SceneComponentPropertyBindings"));
    return translated.value().first->properties->currentRevision(translated.value().second);
}

PropertySchema SceneComponentPropertyBindings::schema(const SelectionSnapshot& selection) const {
    auto translated = translate(selection);
    if (!translated.ok()) return {};
    return translated.value().first->properties->schema(translated.value().second);
}

PropertyReadResult SceneComponentPropertyBindings::read(const SelectionSnapshot& selection,
                                                        const PropertyPath&      path) const {
    auto translated = translate(selection);
    if (!translated.ok()) return {PropertyReadState::Error, {}, translated.diagnostics()};
    return translated.value().first->properties->read(translated.value().second, path);
}

Result<DomainOperation> SceneComponentPropertyBindings::makeSet(const SelectionSnapshot& selection,
                                                                const PropertyPath& path, const EditorValue& value,
                                                                PropertySetMode mode) const {
    auto translated = translate(selection);
    if (!translated.ok()) return Result<DomainOperation>::failure(translated.status());
    return translated.value().first->properties->makeSet(translated.value().second, path, value, mode);
}

Result<DomainOperation> SceneComponentPropertyBindings::makeReset(const SelectionSnapshot& selection,
                                                                  const PropertyPath&      path) const {
    auto translated = translate(selection);
    if (!translated.ok()) return Result<DomainOperation>::failure(translated.status());
    return translated.value().first->properties->makeReset(translated.value().second, path);
}

Result<void> SceneComponentPayloadRegistry::registerProvider(ISceneComponentPayloadProvider* provider) {
    if (!provider || provider->componentType().empty())
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.scene.component-provider-invalid"),
                                          "Component payload provider and type are required");
    const auto [iterator, inserted] = providers_.emplace(provider->componentType(), provider);
    if (!inserted)
        return eve::editing::failed<void>(
            EditorStatus::Conflict, RuleId("editor.scene.component-provider-duplicate"),
            "A component payload provider is already registered for type: " + iterator->first);
    return eve::editing::applied<void>();
}

SceneComponentChange SceneComponentPayloadRegistry::unregisterProvider(ISceneComponentPayloadProvider* provider) {
    if (!provider) return SceneComponentChange::Unchanged;
    const auto found = providers_.find(provider->componentType());
    if (found == providers_.end() || found->second != provider) return SceneComponentChange::Unchanged;
    providers_.erase(found);
    return SceneComponentChange::Changed;
}

Result<std::vector<SceneComponentPayloadRef>> SceneComponentPayloadRegistry::componentPayloads(
    const TargetId& scene, const ObjectId& object) const {
    if (scene.empty() || object.empty())
        return eve::editing::failed<std::vector<SceneComponentPayloadRef>>(
            EditorStatus::Rejected, RuleId("editor.scene.component-owner-invalid"),
            "Scene target and object ids are required for component discovery");
    std::vector<SceneComponentPayloadRef> result;
    for (const auto& [type, provider] : providers_) {
        auto components = provider->components(scene, object);
        for (SceneComponentPayloadRef& component : components) {
            if (component.target != scene || component.object != object || component.type != type ||
                component.component.empty())
                return eve::editing::failed<std::vector<SceneComponentPayloadRef>>(
                    EditorStatus::Failed, RuleId("editor.scene.component-provider-contract"),
                    "Component provider returned an invalid or foreign stable reference: " + type);
            result.push_back(std::move(component));
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.type != right.type) return left.type < right.type;
        return left.component < right.component;
    });
    return eve::editing::applied<std::vector<SceneComponentPayloadRef>>(std::move(result));
}

Result<ISceneComponentPayloadProvider*> SceneComponentPayloadRegistry::resolve(
    const SelectionSnapshot& selection) const {
    if (selection.items.empty())
        return eve::editing::failed<ISceneComponentPayloadProvider*>(EditorStatus::Rejected,
                                                                     RuleId("editor.scene.component-selection-empty"),
                                                                     "Component property selection cannot be empty");
    const std::string& type = selection.items.front().type;
    if (type.empty())
        return eve::editing::failed<ISceneComponentPayloadProvider*>(
            EditorStatus::Rejected, RuleId("editor.scene.component-selection-type"),
            "Component property selection requires a stable component type");
    for (const SelectionItem& item : selection.items) {
        if (item.domain != SelectionDomain::Scene || item.type != type || item.item.empty())
            return eve::editing::failed<ISceneComponentPayloadProvider*>(
                EditorStatus::Rejected, RuleId("editor.scene.component-selection-mixed"),
                "Component property selections must contain one non-empty component type");
    }
    const auto found = providers_.find(type);
    if (found == providers_.end())
        return eve::editing::failed<ISceneComponentPayloadProvider*>(
            EditorStatus::Unsupported, RuleId("editor.scene.component-provider-missing"),
            "No component payload provider is registered for type: " + type);
    return eve::editing::applied<ISceneComponentPayloadProvider*>(found->second);
}

Result<IPropertyProvider*> SceneComponentPayloadRegistry::propertyProvider(const SelectionSnapshot& selection) const {
    auto provider = resolve(selection);
    if (!provider.ok()) return Result<IPropertyProvider*>::failure(provider.status());
    return eve::editing::applied<IPropertyProvider*>(static_cast<IPropertyProvider*>(provider.value()));
}

Result<IDomainOperationTarget*> SceneComponentPayloadRegistry::operationTarget(
    const SelectionSnapshot& selection) const {
    auto provider = resolve(selection);
    if (!provider.ok()) return Result<IDomainOperationTarget*>::failure(provider.status());
    return provider.value()->payloadOperationTarget(selection);
}

Result<std::vector<EditorDiagnostic>> SceneComponentPayloadRegistry::validatePayload(
    const SceneComponentPayloadRef& component) const {
    const auto found = providers_.find(component.type);
    if (found == providers_.end())
        return eve::editing::failed<std::vector<EditorDiagnostic>>(
            EditorStatus::Unsupported, RuleId("editor.scene.component-provider-missing"),
            "No component payload provider is registered for type: " + component.type);
    const auto current  = found->second->components(component.target, component.object);
    const auto matching = std::find_if(current.begin(), current.end(), [&](const auto& candidate) {
        return candidate.component == component.component && candidate.type == component.type;
    });
    if (matching == current.end())
        return eve::editing::failed<std::vector<EditorDiagnostic>>(
            EditorStatus::NotFound, RuleId("editor.scene.component-reference-missing"),
            "The referenced component no longer exists: " + component.component.value());
    if (matching->generation != component.generation || matching->revision != component.revision)
        return eve::editing::failed<std::vector<EditorDiagnostic>>(
            EditorStatus::Conflict, RuleId("editor.scene.component-reference-stale"),
            "The component changed since its inspector reference was captured: " + component.component.value());
    return eve::editing::applied<std::vector<EditorDiagnostic>>(found->second->validateComponent(component));
}

Result<SelectionSnapshot> makeSceneComponentSelection(std::string                                  channel,
                                                      const std::vector<SceneComponentPayloadRef>& components,
                                                      std::uint64_t                                sequence) {
    if (components.empty())
        return eve::editing::failed<SelectionSnapshot>(EditorStatus::Rejected,
                                                       RuleId("editor.scene.component-selection-empty"),
                                                       "At least one component is required");
    const std::string& type = components.front().type;
    SelectionSnapshot  result;
    result.channel  = std::move(channel);
    result.sequence = sequence;
    for (const SceneComponentPayloadRef& component : components) {
        if (component.component.empty() || component.target.empty() || component.type.empty() || component.type != type)
            return eve::editing::failed<SelectionSnapshot>(EditorStatus::Rejected,
                                                           RuleId("editor.scene.component-selection-mixed"),
                                                           "All selected components must have one non-empty type");
        result.items.push_back({SelectionDomain::Scene, component.target, component.component, component.type});
    }
    result.primary = result.items.front();
    return eve::editing::applied<SelectionSnapshot>(std::move(result));
}

}  // namespace eve::scene_editing

// SceneEditingCommands.cpp
#include "scene/editing/SceneAuthoring.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editing/SceneEditingCommands.h"
#include "scene/editing/SceneTarget.h"

#include <cstdint>
#include <set>

namespace eve::scene_editing {
namespace {

const editing::Value* field(const editing::Value& value, const char* key) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

bool assignVector3(const editing::Value& payload, const char* key, double& x, double& y, double& z) {
    const editing::Value* entry = field(payload, key);
    if (!entry) return true;
    const auto* values = entry->getIf<editing::Value::Array>();
    if (!values || values->size() != 3) return false;
    const auto number = [](const editing::Value& value, double& output) {
        if (const auto* real = value.getIf<double>()) {
            output = *real;
            return true;
        }
        if (const auto* integer = value.getIf<std::int64_t>()) {
            output = static_cast<double>(*integer);
            return true;
        }
        return false;
    };
    return number((*values)[0], x) && number((*values)[1], y) && number((*values)[2], z);
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId("scene.transform.set.v1");
    descriptor.ownerModule       = "scene_editing";
    descriptor.displayName       = "Set scene transform";
    descriptor.category          = "Scene";
    descriptor.automationAllowed = true;
    auto transformRegistered     = registry.registerPlannedCommand(
        std::move(descriptor), [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* capability =
                static_cast<ITransformEditTarget*>(target.queryCapability(ITransformEditTarget::editingCapabilityId()));
            const editing::Value* objectValue = field(request.payload, "object");
            const auto*           object      = objectValue ? objectValue->getIf<std::string>() : nullptr;
            if (!capability || !object)
                return eve::editing::failed<editing::CommandPlan>(
                    editing::Status::Rejected, editing::RuleId("scene.editing.transform-payload"),
                    "Scene transform requires a transform target and object id");
            auto current = capability->readTransform(editing::ObjectId(*object));
            if (!current.ok())
                return eve::editing::failed<editing::CommandPlan>(current.code(),
                                                                      editing::RuleId("scene.editing.transform-object"),
                                                                      "Scene object transform is unavailable");
            SceneTransformValue transform = current.value();
            if (!assignVector3(request.payload, "position", transform.x, transform.y, transform.z) ||
                !assignVector3(request.payload, "rotation", transform.rotationX, transform.rotationY,
                                   transform.rotationZ) ||
                !assignVector3(request.payload, "scale", transform.scaleX, transform.scaleY, transform.scaleZ))
                return eve::editing::failed<editing::CommandPlan>(
                    editing::Status::Rejected, editing::RuleId("scene.editing.transform-vector"),
                    "Position, rotation and scale must contain three numbers");
            auto operation = capability->makeSetTransform(editing::ObjectId(*object), transform);
            if (!operation.ok())
                return eve::editing::failed<editing::CommandPlan>(operation.code(),
                                                                      editing::RuleId("scene.editing.transform-operation"),
                                                                      "Scene target rejected the transform");
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation.value()));
            plan.summary = editing::Value::Object{{"object", *object}};
            return eve::editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!transformRegistered.ok()) return transformRegistered;
    editing::EditingCommandDescriptor batchTransform;
    batchTransform.id                = editing::CommandId("scene.transform.batch.set.v1");
    batchTransform.ownerModule       = "scene_editing";
    batchTransform.displayName       = "Set scene transforms";
    batchTransform.category          = "Scene";
    batchTransform.automationAllowed = true;
    auto batchRegistered             = registry.registerPlannedCommand(
        std::move(batchTransform), [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto        capability = target.capability<ITransformEditTarget>();
            const auto* values     = field(request.payload, "transforms");
            const auto* transforms = values ? values->getIf<editing::Value::Array>() : nullptr;
            if (!capability || !transforms || transforms->empty())
                return eve::editing::failed<editing::CommandPlan>(
                    editing::Status::Rejected, editing::RuleId("scene.editing.batch-transform-payload"),
                    "Batch transform requires a nonempty transforms array");
            editing::CommandPlan        plan;
            std::set<editing::ObjectId> ids;
            for (const auto& value : *transforms) {
                const auto* objectValue = field(value, "object");
                const auto* object      = objectValue ? objectValue->getIf<std::string>() : nullptr;
                if (!object || object->empty() || !ids.emplace(*object).second)
                    return eve::editing::failed<editing::CommandPlan>(
                        editing::Status::Rejected, editing::RuleId("scene.editing.batch-transform-object"),
                        "Batch transform object ids must be nonempty and unique");
                auto current = capability->get().readTransform(editing::ObjectId(*object));
                if (!current.ok()) return editing::Result<editing::CommandPlan>::failure(current.status());
                auto transform = current.value();
                if (!assignVector3(value, "position", transform.x, transform.y, transform.z) ||
                    !assignVector3(value, "rotation", transform.rotationX, transform.rotationY, transform.rotationZ) ||
                    !assignVector3(value, "scale", transform.scaleX, transform.scaleY, transform.scaleZ))
                    return eve::editing::failed<editing::CommandPlan>(
                        editing::Status::Rejected, editing::RuleId("scene.editing.batch-transform-vector"),
                        "Batch TRS vectors must contain three numbers");
                auto operation = capability->get().makeSetTransform(editing::ObjectId(*object), transform);
                if (!operation.ok()) return editing::Result<editing::CommandPlan>::failure(operation.status());
                plan.operations.push_back(std::move(operation.value()));
            }
            plan.summary = editing::Value::Object{{"count", static_cast<std::int64_t>(transforms->size())}};
            return editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!batchRegistered.ok()) return batchRegistered;
    for (const std::string action : {"create", "delete", "rename", "reparent"}) {
        editing::EditingCommandDescriptor entry;
        entry.id          = editing::CommandId("scene.object." + action + ".v1");
        entry.ownerModule = "scene_editing";
        entry.displayName = "Scene object " + action;
        entry.category    = "Scene";
        auto registered   = registry.registerPlannedCommand(
            std::move(entry), [action](editing::IEditableTarget& target, const editing::CommandRequest& request) {
                auto hierarchy = target.capability<ISceneHierarchyEditTarget>();
                if (!hierarchy)
                    return eve::editing::failed<editing::CommandPlan>(
                        editing::Status::Unsupported, editing::RuleId("scene.editing.hierarchy-unavailable"),
                        "Target does not support scene hierarchy editing");
                const auto* idValue = field(request.payload, "object");
                const auto* id      = idValue ? idValue->getIf<std::string>() : nullptr;
                if (!id || id->empty())
                    return eve::editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                        editing::RuleId("scene.editing.object-required"),
                                                                        "A nonempty object id is required");
                const auto* nameValue   = field(request.payload, "name");
                const auto* name        = nameValue ? nameValue->getIf<std::string>() : nullptr;
                const auto* parentValue = field(request.payload, "parent");
                const auto* parent      = parentValue ? parentValue->getIf<std::string>() : nullptr;
                if ((nameValue && !name) || (parentValue && !parent) || (action == "rename" && !name) ||
                    (action == "reparent" && !parent))
                    return eve::editing::failed<editing::CommandPlan>(
                        editing::Status::Rejected, editing::RuleId("scene.editing.hierarchy-payload"),
                        "Name and parent must be strings");
                auto operation = [&]() -> editing::Result<editing::DomainOperation> {
                    if (action == "delete") return hierarchy->get().makeDelete(editing::ObjectId(*id));
                    if (action == "rename") return hierarchy->get().makeRename(editing::ObjectId(*id), *name);
                    if (action == "reparent")
                        return hierarchy->get().makeReparent(editing::ObjectId(*id), editing::ObjectId(*parent));
                    CreateSceneObjectRequest create;
                    create.id     = editing::ObjectId(*id);
                    create.name   = name ? *name : *id;
                    create.parent = parent ? editing::ObjectId(*parent) : editing::ObjectId{};
                    auto& t       = create.transform;
                    if (!assignVector3(request.payload, "position", t.x, t.y, t.z) ||
                        !assignVector3(request.payload, "rotation", t.rotationX, t.rotationY, t.rotationZ) ||
                        !assignVector3(request.payload, "scale", t.scaleX, t.scaleY, t.scaleZ))
                        return eve::editing::failed<editing::DomainOperation>(
                            editing::Status::Rejected, editing::RuleId("scene.editing.create-vector"),
                            "TRS vectors require three numbers");
                    return hierarchy->get().makeCreate(create);
                }();
                if (!operation.ok()) return editing::Result<editing::CommandPlan>::failure(operation.status());
                editing::CommandPlan plan;
                plan.operations.push_back(std::move(operation.value()));
                return editing::applied<editing::CommandPlan>(std::move(plan));
            });
        if (!registered.ok()) return registered;
    }
    editing::EditingCommandDescriptor update;
    update.id          = editing::CommandId("scene.object.update.v1");
    update.ownerModule = "scene_editing";
    update.displayName = "Edit scene object properties";
    update.category    = "Scene";
    auto updated       = registry.registerPlannedCommand(
        std::move(update),
        [](editing::IEditableTarget&      target,
           const editing::CommandRequest& request) -> editing::Result<editing::CommandPlan> {
            auto        hierarchy  = target.capability<ISceneHierarchyEditTarget>();
            auto        transforms = target.capability<ITransformEditTarget>();
            const auto* idValue    = field(request.payload, "object");
            const auto* id         = idValue ? idValue->getIf<std::string>() : nullptr;
            if (!hierarchy || !transforms || !id)
                return eve::editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                        editing::RuleId("scene.editing.update-payload"),
                                                                        "Scene object and editing capabilities are required");
            auto current = transforms->get().readTransform(editing::ObjectId(*id));
            if (!current.ok()) return editing::Result<editing::CommandPlan>::failure(current.status());
            auto t = current.value();
            if (!assignVector3(request.payload, "position", t.x, t.y, t.z) ||
                !assignVector3(request.payload, "rotation", t.rotationX, t.rotationY, t.rotationZ) ||
                !assignVector3(request.payload, "scale", t.scaleX, t.scaleY, t.scaleZ))
                return eve::editing::failed<editing::CommandPlan>(
                    editing::Status::Rejected, editing::RuleId("scene.editing.update-vector"), "Invalid TRS vector");
            auto transform = transforms->get().makeSetTransform(editing::ObjectId(*id), t);
            if (!transform.ok()) return editing::Result<editing::CommandPlan>::failure(transform.status());
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(transform.value()));
            for (const auto* key : {"name", "parent"}) {
                const auto* value = field(request.payload, key);
                if (!value) continue;
                const auto* text = value->getIf<std::string>();
                if (!text)
                    return eve::editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                            editing::RuleId("scene.editing.update-text"),
                                                                            "Name and parent must be strings");
                auto operation = std::string_view(key) == "name"
                                           ? hierarchy->get().makeRename(editing::ObjectId(*id), *text)
                                           : hierarchy->get().makeReparent(editing::ObjectId(*id), editing::ObjectId(*text));
                if (!operation.ok()) return editing::Result<editing::CommandPlan>::failure(operation.status());
                plan.operations.push_back(std::move(operation.value()));
            }
            return editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!updated.ok()) return updated;
    editing::EditingCommandDescriptor restore;
    restore.id          = editing::CommandId("scene.snapshot.restore.v1");
    restore.ownerModule = "scene_editing";
    restore.displayName = "Restore scene hierarchy and transforms";
    restore.category    = "Scene";
    auto restored       = registry.registerPlannedCommand(
        std::move(restore),
        [](editing::IEditableTarget&      value,
           const editing::CommandRequest& request) -> editing::Result<editing::CommandPlan> {
            auto* scene = dynamic_cast<SceneTargetBase*>(&value);
            if (!scene)
                return eve::editing::failed<editing::CommandPlan>(editing::Status::Unsupported,
                                                                        editing::RuleId("scene.restore.target"),
                                                                        "Target does not support scene snapshots");
            auto operation = scene->makeRestore(request.payload);
            if (!operation.ok()) return editing::Result<editing::CommandPlan>::failure(operation.status());
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation.value()));
            return editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!restored.ok()) return restored;
    auto placement = registerAuthoringCommands(registry);
    if (!placement.ok()) return placement;
    return registerAuthoringDocumentCommands(registry);
}

}  // namespace eve::scene_editing

// SceneSnapshot.cpp
namespace eve::scene_editing {

Result<std::map<ObjectId, SceneObjectSnapshot>> SceneTargetBase::parseSnapshot(const EditorValue& value) {
    using Objects     = std::map<ObjectId, SceneObjectSnapshot>;
    const auto reject = [](const char* message) {
        return editing::failed<Objects>(EditorStatus::Rejected, RuleId("scene.snapshot.invalid"), message);
    };
    const auto* root = value.getIf<EditorValue::Object>();
    if (!root || !root->contains("schemaVersion") || !root->contains("objects") ||
        root->size() != (root->contains("schemaId") ? 3u : 2u))
        return reject("Scene snapshot requires schemaId, schemaVersion and objects");
    if (root->contains("schemaId")) {
        const auto* schema = root->at("schemaId").getIf<std::string>();
        if (!schema || *schema != "eve.scene.hierarchy") return reject("Unknown scene schema id");
    }
    const auto* version = root->at("schemaVersion").getIf<std::int64_t>();
    const auto* entries = root->at("objects").getIf<EditorValue::Array>();
    if (!version || *version != 1 || !entries) return reject("Unsupported scene snapshot schema");
    Objects objects;
    for (const auto& entry : *entries) {
        const auto* fields = entry.getIf<EditorValue::Object>();
        if (!fields || fields->size() != 4 || !fields->contains("transform"))
            return reject("Scene object requires id, parent, name and transform");
        const auto* transform = fields->at("transform").getIf<EditorValue::Object>();
        if (!transform || transform->size() != 9) return reject("Scene transform requires all nine TRS fields");
        // JSON parsers may decode integral floating point values as integers.
        EditorValue normalized = entry;
        auto&       normalizedTransform =
            *normalized.getIf<EditorValue::Object>()->at("transform").getIf<EditorValue::Object>();
        for (const auto* key : {"x", "y", "z", "rotationX", "rotationY", "rotationZ", "scaleX", "scaleY", "scaleZ"}) {
            const auto found = transform->find(key);
            if (found == transform->end()) return reject("Unknown or missing transform field");
            double number;
            if (const auto* real = found->second.getIf<double>())
                number = *real;
            else if (const auto* integer = found->second.getIf<std::int64_t>())
                number = static_cast<double>(*integer);
            else
                return reject("Transform fields must be numbers");
            if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
                return reject("Transform must be finite and representable by the scene runtime");
            if (std::string_view(key).starts_with("scale") && static_cast<float>(number) == 0.f)
                return reject("Scene scale must be nonzero");
            normalizedTransform[key] = number;
        }
        auto parsed = parseObject(normalized);
        if (!parsed.ok()) return Result<Objects>::failure(parsed.status());
        auto object = std::move(parsed.value());
        if (object.id.empty() || object.name.empty() || objects.contains(object.id))
            return reject("Object ids must be unique and names nonempty");
        objects.emplace(object.id, std::move(object));
    }
    for (const auto& [id, object] : objects) {
        std::set<ObjectId> visited{id};
        auto               parent = object.parent;
        while (!parent.empty()) {
            if (!objects.contains(parent)) return reject("Scene parent does not exist");
            if (!visited.insert(parent).second) return reject("Scene hierarchy contains a cycle");
            parent = objects.at(parent).parent;
        }
    }
    return editing::applied<Objects>(std::move(objects));
}

Result<DomainOperation> SceneTargetBase::makeRestore(const EditorValue& snapshot) const {
    auto validated = parseSnapshot(snapshot);
    if (!validated.ok()) return Result<DomainOperation>::failure(validated.status());
    DomainOperation operation;
    operation.type = operation.inverseType = "scene.snapshot.restore.v1";
    operation.target                       = targetId();
    operation.payload                      = snapshot;
    operation.inverse                      = snapshotValue();
    operation.hasInverse                   = true;
    for (const auto& [id, object] : objects_) operation.affectedObjects.push_back({targetId(), id.value(), 0});
    for (const auto& [id, object] : validated.value())
        if (!objects_.contains(id)) operation.affectedObjects.push_back({targetId(), id.value(), 0});
    return editing::applied<DomainOperation>(std::move(operation));
}
}  // namespace eve::scene_editing
