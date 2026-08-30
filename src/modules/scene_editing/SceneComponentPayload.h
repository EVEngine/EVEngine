#pragma once

#include "scene_editing/SceneEditingTypes.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace eve::scene_editing {

/** @brief Whether a registry or binding removal changed authoritative state. */
enum class SceneComponentChange { Changed, Unchanged };

/** @brief Stable, pointer-free reference to one editable component instance. */
struct SceneComponentPayloadRef {
    TargetId target;
    ObjectId object;
    StableId component;
    std::string type;
    std::uint64_t generation = 0;
    Revision revision = 0;

    auto operator<=>(const SceneComponentPayloadRef&) const = default;
};

/**
 * @brief Module-owned adapter for one scene component payload type.
 *
 * Implementations retain ownership of component data and expose it through the
 * shared property and operation protocols. Editable adapters should also
 * implement IDomainOperationTargetStaging so standard undo remains atomic.
 */
class ISceneComponentPayloadProvider : public IPropertyProvider {
public:
    ~ISceneComponentPayloadProvider() override = default;

    /** @brief Stable component type handled by this provider. */
    virtual const std::string& componentType() const = 0;
    /** @brief Enumerate immutable component references attached to one object. */
    virtual std::vector<SceneComponentPayloadRef> components(const TargetId& scene,
                                                              const ObjectId& object) const = 0;
    /** @brief Resolve the authoritative mutation target for one component selection. */
    virtual EditorResult<IDomainOperationTarget*> payloadOperationTarget(
        const SelectionSnapshot& selection) const = 0;
    /** @brief Validate the current payload and return inspector diagnostics. */
    virtual std::vector<EditorDiagnostic> validateComponent(const SceneComponentPayloadRef& component) const = 0;
};

/**
 * @brief Reuses existing property/operation targets as scene component payloads.
 *
 * This adapter keeps no duplicate component values. It translates stable scene
 * component selections to the selection expected by the module-owned target.
 */
class SceneComponentPropertyBindings final : public ISceneComponentPayloadProvider {
public:
    using Validator = std::function<std::vector<EditorDiagnostic>()>;

    /** @brief Create bindings for one stable component type. */
    explicit SceneComponentPropertyBindings(std::string componentType);
    const std::string& componentType() const override { return componentType_; }

    /** @brief Bind one stable scene component to an existing module editor target. */
    EditorResult<void> bind(SceneComponentPayloadRef component,
                            SelectionItem moduleSelection,
                            IPropertyProvider* properties,
                            IDomainOperationTarget* operations,
                            Validator validator = {});
    /** @brief Remove one exact scene component binding. */
    SceneComponentChange unbind(const TargetId& scene, const StableId& component);

    std::vector<SceneComponentPayloadRef> components(const TargetId& scene,
                                                      const ObjectId& object) const override;
    EditorResult<IDomainOperationTarget*> payloadOperationTarget(
        const SelectionSnapshot& selection) const override;
    std::vector<EditorDiagnostic> validateComponent(
        const SceneComponentPayloadRef& component) const override;
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection,
                                          const PropertyPath& path, const EditorValue& value,
                                          PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;

private:
    struct Binding {
        SceneComponentPayloadRef component;
        SelectionItem moduleSelection;
        IPropertyProvider* properties = nullptr;
        IDomainOperationTarget* operations = nullptr;
        Validator validator;
    };

    /** @brief Translate a scene selection. @return Result containing a borrowed binding pointer. @lifetime The pointer is valid until this binding collection is mutated or destroyed. */
    EditorResult<std::pair<const Binding*, SelectionSnapshot>> translate(
        const SelectionSnapshot& selection) const;
    /** @brief Find an exact binding. @return Borrowed pointer into this collection, or null. @lifetime Valid until the collection is mutated or destroyed. */
    const Binding* find(const TargetId& scene, const StableId& component) const;

    std::string componentType_;
    std::vector<Binding> bindings_;
};

/** @brief Scene capability that discovers and routes module-owned component payload editors. */
class ISceneComponentPayloadTarget {
public:
    virtual ~ISceneComponentPayloadTarget() = default;
    /** @brief Stable capability id for editable scene component payloads. */
    static CapabilityId editorCapabilityId() {
        return CapabilityId("eve.editor.target.scene-component-payloads");
    }
    /** @brief Enumerate all registered component payloads on one scene object. */
    virtual EditorResult<std::vector<SceneComponentPayloadRef>> componentPayloads(
        const TargetId& scene, const ObjectId& object) const = 0;
    /** @brief Resolve the property provider for a homogeneous component selection. */
    virtual EditorResult<IPropertyProvider*> propertyProvider(const SelectionSnapshot& selection) const = 0;
    /** @brief Resolve the operation target that must receive the provider's planned operations. */
    virtual EditorResult<IDomainOperationTarget*> operationTarget(const SelectionSnapshot& selection) const = 0;
    /** @brief Validate a stable component reference using its owning module. */
    virtual EditorResult<std::vector<EditorDiagnostic>> validatePayload(
        const SceneComponentPayloadRef& component) const = 0;
};

/** @brief Non-owning registry and scene-facing router for component payload providers. */
class SceneComponentPayloadRegistry final : public ISceneComponentPayloadTarget {
public:
    /** @brief Register one provider; component types must be non-empty and unique. */
    EditorResult<void> registerProvider(ISceneComponentPayloadProvider* provider);
    /** @brief Remove a provider only when the exact registered instance matches. */
    SceneComponentChange unregisterProvider(ISceneComponentPayloadProvider* provider);

    EditorResult<std::vector<SceneComponentPayloadRef>> componentPayloads(
        const TargetId& scene, const ObjectId& object) const override;
    EditorResult<IPropertyProvider*> propertyProvider(const SelectionSnapshot& selection) const override;
    EditorResult<IDomainOperationTarget*> operationTarget(const SelectionSnapshot& selection) const override;
    EditorResult<std::vector<EditorDiagnostic>> validatePayload(
        const SceneComponentPayloadRef& component) const override;

private:
    EditorResult<ISceneComponentPayloadProvider*> resolve(const SelectionSnapshot& selection) const;
    std::map<std::string, ISceneComponentPayloadProvider*> providers_;
};

/** @brief Build a property selection for one or more homogeneous component references. */
EditorResult<SelectionSnapshot> makeSceneComponentSelection(
    std::string channel, const std::vector<SceneComponentPayloadRef>& components,
    std::uint64_t sequence = 0);

}  // namespace eve::scene_editing
