#include "scene_editing/SceneComponentPayload.h"

#include <algorithm>

namespace eve::scene_editing {
namespace {

template <class T>
EditorResult<T> payloadError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

}  // namespace

SceneComponentPropertyBindings::SceneComponentPropertyBindings(std::string componentType)
    : componentType_(std::move(componentType)) {}

EditorResult<void> SceneComponentPropertyBindings::bind(
    SceneComponentPayloadRef component, SelectionItem moduleSelection,
    IPropertyProvider* properties, IDomainOperationTarget* operations, Validator validator) {
    if (componentType_.empty() || component.target.empty() || component.object.empty() ||
        component.component.empty() || component.type != componentType_ || !properties || !operations ||
        moduleSelection.target != TargetId(operations->targetId()))
        return payloadError<void>(EditorStatus::Rejected, "editor.scene.component-binding-invalid",
                                  "Component binding requires matching stable ids and module targets");
    if (find(component.target, component.component))
        return payloadError<void>(EditorStatus::Conflict, "editor.scene.component-binding-duplicate",
                                  "Scene component is already bound: " + component.component.value());
    component.revision = operations->revision();
    bindings_.push_back({std::move(component), std::move(moduleSelection), properties, operations,
                         std::move(validator)});
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

const SceneComponentPropertyBindings::Binding* SceneComponentPropertyBindings::find(
    const TargetId& scene, const StableId& component) const {
    const auto found = std::find_if(bindings_.begin(), bindings_.end(), [&](const Binding& binding) {
        return binding.component.target == scene && binding.component.component == component;
    });
    return found == bindings_.end() ? nullptr : &*found;
}

std::vector<SceneComponentPayloadRef> SceneComponentPropertyBindings::components(
    const TargetId& scene, const ObjectId& object) const {
    std::vector<SceneComponentPayloadRef> result;
    for (const Binding& binding : bindings_) {
        if (binding.component.target != scene || binding.component.object != object) continue;
        SceneComponentPayloadRef component = binding.component;
        component.revision = binding.operations->revision();
        result.push_back(std::move(component));
    }
    return result;
}

EditorResult<std::pair<const SceneComponentPropertyBindings::Binding*, SelectionSnapshot>>
SceneComponentPropertyBindings::translate(const SelectionSnapshot& selection) const {
    if (selection.items.empty())
        return payloadError<std::pair<const Binding*, SelectionSnapshot>>(
            EditorStatus::Rejected, "editor.scene.component-selection-empty",
            "Component property selection cannot be empty");
    const Binding* first = find(selection.items.front().target, selection.items.front().item);
    if (!first || selection.items.front().type != componentType_)
        return payloadError<std::pair<const Binding*, SelectionSnapshot>>(
            EditorStatus::NotFound, "editor.scene.component-binding-missing",
            "Selected scene component is not bound to this provider");
    SelectionSnapshot translated;
    translated.channel = selection.channel;
    translated.sequence = selection.sequence;
    for (const SelectionItem& item : selection.items) {
        const Binding* binding = find(item.target, item.item);
        if (!binding || item.type != componentType_ || binding->properties != first->properties ||
            binding->operations != first->operations)
            return payloadError<std::pair<const Binding*, SelectionSnapshot>>(
                EditorStatus::Rejected, "editor.scene.component-binding-mixed-target",
                "Multi-edit requires components owned by one module property target");
        translated.items.push_back(binding->moduleSelection);
    }
    translated.primary = translated.items.front();
    return eve::editing::applied<std::pair<const Binding*, SelectionSnapshot>>(
        {first, std::move(translated)});
}

EditorResult<IDomainOperationTarget*> SceneComponentPropertyBindings::payloadOperationTarget(
    const SelectionSnapshot& selection) const {
    auto translated = translate(selection);
    if (!translated.ok()) return EditorResult<IDomainOperationTarget*>::failure(translated.status());
    return eve::editing::applied<IDomainOperationTarget*>(translated.value().first->operations);
}

std::vector<EditorDiagnostic> SceneComponentPropertyBindings::validateComponent(
    const SceneComponentPayloadRef& component) const {
    const Binding* binding = find(component.target, component.component);
    if (!binding)
        return {eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::NotFound, RuleId("editor.scene.component-binding-missing"),
            DiagnosticSeverity::Error, "Scene component binding no longer exists")};
    return binding->validator ? binding->validator() : std::vector<EditorDiagnostic>{};
}

eve::Result<eve::Revision> SceneComponentPropertyBindings::currentRevision(
    const SelectionSnapshot& selection) const {
    auto translated = translate(selection);
    if (!translated.ok())
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Scene component selection is not bound",
            "editor.scene.component-binding", {}, "editor.SceneComponentPropertyBindings"));
    return translated.value().first->properties->currentRevision(translated.value().second);
}

PropertySchema SceneComponentPropertyBindings::schema(const SelectionSnapshot& selection) const {
    auto translated = translate(selection);
    if (!translated.ok()) return {};
    return translated.value().first->properties->schema(translated.value().second);
}

PropertyReadResult SceneComponentPropertyBindings::read(const SelectionSnapshot& selection,
                                                         const PropertyPath& path) const {
    auto translated = translate(selection);
    if (!translated.ok()) return {PropertyReadState::Error, {}, translated.diagnostics()};
    return translated.value().first->properties->read(translated.value().second, path);
}

EditorResult<DomainOperation> SceneComponentPropertyBindings::makeSet(
    const SelectionSnapshot& selection, const PropertyPath& path, const EditorValue& value,
    PropertySetMode mode) const {
    auto translated = translate(selection);
    if (!translated.ok()) return EditorResult<DomainOperation>::failure(translated.status());
    return translated.value().first->properties->makeSet(translated.value().second, path, value, mode);
}

EditorResult<DomainOperation> SceneComponentPropertyBindings::makeReset(
    const SelectionSnapshot& selection, const PropertyPath& path) const {
    auto translated = translate(selection);
    if (!translated.ok()) return EditorResult<DomainOperation>::failure(translated.status());
    return translated.value().first->properties->makeReset(translated.value().second, path);
}

EditorResult<void> SceneComponentPayloadRegistry::registerProvider(
    ISceneComponentPayloadProvider* provider) {
    if (!provider || provider->componentType().empty())
        return payloadError<void>(EditorStatus::Rejected, "editor.scene.component-provider-invalid",
                                  "Component payload provider and type are required");
    const auto [iterator, inserted] = providers_.emplace(provider->componentType(), provider);
    if (!inserted)
        return payloadError<void>(EditorStatus::Conflict, "editor.scene.component-provider-duplicate",
                                  "A component payload provider is already registered for type: " +
                                      iterator->first);
    return eve::editing::applied<void>();
}

SceneComponentChange SceneComponentPayloadRegistry::unregisterProvider(ISceneComponentPayloadProvider* provider) {
    if (!provider) return SceneComponentChange::Unchanged;
    const auto found = providers_.find(provider->componentType());
    if (found == providers_.end() || found->second != provider) return SceneComponentChange::Unchanged;
    providers_.erase(found);
    return SceneComponentChange::Changed;
}

EditorResult<std::vector<SceneComponentPayloadRef>> SceneComponentPayloadRegistry::componentPayloads(
    const TargetId& scene, const ObjectId& object) const {
    if (scene.empty() || object.empty())
        return payloadError<std::vector<SceneComponentPayloadRef>>(
            EditorStatus::Rejected, "editor.scene.component-owner-invalid",
            "Scene target and object ids are required for component discovery");
    std::vector<SceneComponentPayloadRef> result;
    for (const auto& [type, provider] : providers_) {
        auto components = provider->components(scene, object);
        for (SceneComponentPayloadRef& component : components) {
            if (component.target != scene || component.object != object || component.type != type ||
                component.component.empty())
                return payloadError<std::vector<SceneComponentPayloadRef>>(
                    EditorStatus::Failed, "editor.scene.component-provider-contract",
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

EditorResult<ISceneComponentPayloadProvider*> SceneComponentPayloadRegistry::resolve(
    const SelectionSnapshot& selection) const {
    if (selection.items.empty())
        return payloadError<ISceneComponentPayloadProvider*>(
            EditorStatus::Rejected, "editor.scene.component-selection-empty",
            "Component property selection cannot be empty");
    const std::string& type = selection.items.front().type;
    if (type.empty())
        return payloadError<ISceneComponentPayloadProvider*>(
            EditorStatus::Rejected, "editor.scene.component-selection-type",
            "Component property selection requires a stable component type");
    for (const SelectionItem& item : selection.items) {
        if (item.domain != SelectionDomain::Scene || item.type != type || item.item.empty())
            return payloadError<ISceneComponentPayloadProvider*>(
                EditorStatus::Rejected, "editor.scene.component-selection-mixed",
                "Component property selections must contain one non-empty component type");
    }
    const auto found = providers_.find(type);
    if (found == providers_.end())
        return payloadError<ISceneComponentPayloadProvider*>(
            EditorStatus::Unsupported, "editor.scene.component-provider-missing",
            "No component payload provider is registered for type: " + type);
    return eve::editing::applied<ISceneComponentPayloadProvider*>(found->second);
}

EditorResult<IPropertyProvider*> SceneComponentPayloadRegistry::propertyProvider(
    const SelectionSnapshot& selection) const {
    auto provider = resolve(selection);
    if (!provider.ok()) return EditorResult<IPropertyProvider*>::failure(provider.status());
    return eve::editing::applied<IPropertyProvider*>(static_cast<IPropertyProvider*>(provider.value()));
}

EditorResult<IDomainOperationTarget*> SceneComponentPayloadRegistry::operationTarget(
    const SelectionSnapshot& selection) const {
    auto provider = resolve(selection);
    if (!provider.ok()) return EditorResult<IDomainOperationTarget*>::failure(provider.status());
    return provider.value()->payloadOperationTarget(selection);
}

EditorResult<std::vector<EditorDiagnostic>> SceneComponentPayloadRegistry::validatePayload(
    const SceneComponentPayloadRef& component) const {
    const auto found = providers_.find(component.type);
    if (found == providers_.end())
        return payloadError<std::vector<EditorDiagnostic>>(
            EditorStatus::Unsupported, "editor.scene.component-provider-missing",
            "No component payload provider is registered for type: " + component.type);
    const auto current = found->second->components(component.target, component.object);
    const auto matching = std::find_if(current.begin(), current.end(), [&](const auto& candidate) {
        return candidate.component == component.component && candidate.type == component.type;
    });
    if (matching == current.end())
        return payloadError<std::vector<EditorDiagnostic>>(
            EditorStatus::NotFound, "editor.scene.component-reference-missing",
            "The referenced component no longer exists: " + component.component.value());
    if (matching->generation != component.generation || matching->revision != component.revision)
        return payloadError<std::vector<EditorDiagnostic>>(
            EditorStatus::Conflict, "editor.scene.component-reference-stale",
            "The component changed since its inspector reference was captured: " +
                component.component.value());
    return eve::editing::applied<std::vector<EditorDiagnostic>>(
        found->second->validateComponent(component));
}

EditorResult<SelectionSnapshot> makeSceneComponentSelection(
    std::string channel, const std::vector<SceneComponentPayloadRef>& components,
    std::uint64_t sequence) {
    if (components.empty())
        return payloadError<SelectionSnapshot>(EditorStatus::Rejected,
                                               "editor.scene.component-selection-empty",
                                               "At least one component is required");
    const std::string& type = components.front().type;
    SelectionSnapshot result;
    result.channel = std::move(channel);
    result.sequence = sequence;
    for (const SceneComponentPayloadRef& component : components) {
        if (component.component.empty() || component.target.empty() || component.type.empty() ||
            component.type != type)
            return payloadError<SelectionSnapshot>(EditorStatus::Rejected,
                                                   "editor.scene.component-selection-mixed",
                                                   "All selected components must have one non-empty type");
        result.items.push_back(
            {SelectionDomain::Scene, component.target, component.component, component.type});
    }
    result.primary = result.items.front();
    return eve::editing::applied<SelectionSnapshot>(std::move(result));
}

}  // namespace eve::scene_editing
