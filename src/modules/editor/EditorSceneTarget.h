#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editing/EditableTarget.h"
#include "scene_editing/SceneEditingCommands.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

class SceneComponentPayloadRegistry;

using SceneTransformValue = eve::scene_editing::SceneTransformValue;
using ITransformEditTarget = eve::scene_editing::ITransformEditTarget;

/** @brief Immutable scene object snapshot returned across target boundaries. */
struct SceneObjectSnapshot {
    ObjectId            id;
    ObjectId            parent;
    std::string         name;
    SceneTransformValue transform;
};

/** @brief Request used to create a scene object in either backend. */
struct CreateSceneObjectRequest {
    ObjectId            id;
    ObjectId            parent;
    std::string         name;
    SceneTransformValue transform;
};

/** @brief Stable hierarchy capability implemented by document and runtime targets. */
class ISceneHierarchyEditTarget {
public:
    virtual ~ISceneHierarchyEditTarget() = default;
    /** @brief Stable capability identity used instead of cross-module RTTI. */
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.scene-hierarchy"); }
    /** @brief Return an immutable object snapshot. */
    virtual EditorResult<SceneObjectSnapshot> sceneObject(const ObjectId& id) const = 0;
    /** @brief Return child identities in deterministic order. */
    virtual std::vector<ObjectId> sceneChildren(const ObjectId& parent) const = 0;
    /** @brief Build a reversible create operation without applying it. */
    virtual EditorResult<DomainOperation> makeCreate(const CreateSceneObjectRequest& request) const = 0;
    /** @brief Build a reversible delete operation for a leaf object. */
    virtual EditorResult<DomainOperation> makeDelete(const ObjectId& id) const = 0;
    /** @brief Build a reversible object rename operation. */
    virtual EditorResult<DomainOperation> makeRename(const ObjectId& id, const std::string& name) const = 0;
    /** @brief Build a reversible hierarchy reparent operation. */
    virtual EditorResult<DomainOperation> makeReparent(const ObjectId& id, const ObjectId& parent) const = 0;
};

/** @brief Safe metadata for one external component link on a live scene object. */
struct SceneComponentLinkSnapshot {
    std::string kind;
    int syncMode = 0;
    bool targetAlive = false;
};

/** @brief Read-only live component/link inspection capability for scene inspectors. */
class ISceneComponentInspector {
public:
    virtual ~ISceneComponentInspector() = default;
    /** @brief Stable capability id for component/link metadata inspection. */
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.scene-components"); }
    /** @brief Enumerate safe link metadata without exposing runtime pointers. */
    virtual EditorResult<std::vector<SceneComponentLinkSnapshot>> componentLinks(
        const ObjectId& object) const = 0;
};

/**
 * @brief Shared in-memory scene target implementation for document/runtime adapters.
 *
 * Concrete subclasses differ only in host-facing target type. The mutation
 * protocol and capabilities stay identical so tools contain no backend branch.
 */
class SceneTargetBase : public virtual IEditableTarget,
                        public IDomainOperationTarget,
                        public IDomainOperationTargetStaging,
                        public eve::editing::IEditingSnapshotProvider,
                        public ISceneHierarchyEditTarget,
                        public ITransformEditTarget {
public:
    SceneTargetBase(std::string id, std::string type);
    ~SceneTargetBase() override = default;

    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion         dirtyRegion() const override { return dirty_; }
    void               clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor   describe() const override;
    void*              queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;

    EditorResult<SceneObjectSnapshot> sceneObject(const ObjectId& id) const override;
    std::vector<ObjectId>             sceneChildren(const ObjectId& parent) const override;
    EditorResult<DomainOperation>     makeCreate(const CreateSceneObjectRequest& request) const override;
    EditorResult<DomainOperation>     makeDelete(const ObjectId& id) const override;
    EditorResult<DomainOperation>     makeRename(const ObjectId& id, const std::string& name) const override;
    EditorResult<DomainOperation>     makeReparent(const ObjectId& id, const ObjectId& parent) const override;
    EditorResult<SceneTransformValue> readTransform(const ObjectId& id) const override;
    EditorResult<DomainOperation>     makeSetTransform(const ObjectId&            id,
                                                       const SceneTransformValue& transform) const override;
    /** @brief Bind an optional non-owning registry used by component inspectors. */
    void bindComponentPayloads(SceneComponentPayloadRegistry* registry) { componentPayloads_ = registry; }
    /** @brief Capture deterministic scene content for document persistence. */
    EditorValue snapshotValue() const override;

private:
    friend class ScenePropertyProvider;
    static EditorValue                       transformValue(const SceneTransformValue& transform);
    static EditorResult<SceneTransformValue> parseTransform(const EditorValue& value);
    static EditorResult<SceneObjectSnapshot> parseObject(const EditorValue& value);
    static EditorValue                       objectValue(const SceneObjectSnapshot& object);

    std::string                             id_;
    std::string                             type_;
    unsigned long long                      revision_ = 0;
    EditRegion                              dirty_;
    std::map<ObjectId, SceneObjectSnapshot> objects_;
    SceneComponentPayloadRegistry*          componentPayloads_ = nullptr;
};

/** @brief Authoring scene-document backend exposing the standard scene capabilities. */
class SceneDocumentTarget final : public SceneTargetBase {
public:
    explicit SceneDocumentTarget(std::string id) : SceneTargetBase(std::move(id), "scene-document") {}
};

/** @brief Live game-world backend exposing the same standard scene capabilities. */
class RuntimeWorldTarget final : public SceneTargetBase {
public:
    explicit RuntimeWorldTarget(std::string id) : SceneTargetBase(std::move(id), "runtime-world") {}
};

}  // namespace eve::editor

namespace eve::scene {
class SceneHost;
}

namespace eve::editor {

/**
 * @brief Optional live adapter mirroring editor operations into one SceneHost.
 *
 * Unlike RuntimeWorldTarget's standalone model, this adapter imports retained
 * nodes and commits incremental mutations without rebuilding external links.
 */
class SceneHostEditorTarget final : public SceneTargetBase, public ISceneComponentInspector {
public:
    /** @brief Import one borrowed SceneHost; the host must outlive this target. */
    /** @brief Wrap a live scene host. @param host Borrowed host, or null for a staging clone. @lifetime A non-null host must outlive this target. */
    SceneHostEditorTarget(std::string id, scene::SceneHost* host);
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Return the borrowed live host, or nullptr for a staging clone. @return Borrowed pointer owned externally, or null. @lifetime Valid only while the host outlives this target. */
    scene::SceneHost* host() const { return host_; }
    EditorResult<std::vector<SceneComponentLinkSnapshot>> componentLinks(
        const ObjectId& object) const override;

private:
    EditorResult<void> synchronizeHost(const SceneTargetBase& desired);
    scene::SceneHost* host_ = nullptr;
};

/** @brief Backend-neutral placement logic suitable for a Tool, script or command handler. */
class ScenePlacementToolLogic {
public:
    /** @brief Query hierarchy capability and build a create operation. */
    EditorResult<DomainOperation> plan(IEditableTarget& target, const CreateSceneObjectRequest& request) const;
};

/** @brief Backend-neutral hierarchy editing logic for outliner-style tools. */
class SceneHierarchyToolLogic {
public:
    /** @brief Query hierarchy capability and build a leaf deletion operation. */
    EditorResult<DomainOperation> planDelete(IEditableTarget& target, const ObjectId& object) const;
    /** @brief Query hierarchy capability and build a rename operation. */
    EditorResult<DomainOperation> planRename(IEditableTarget& target, const ObjectId& object,
                                             const std::string& name) const;
    /** @brief Query hierarchy capability and build a reparent operation. */
    EditorResult<DomainOperation> planReparent(IEditableTarget& target, const ObjectId& object,
                                               const ObjectId& parent) const;
};

/** @brief Backend-neutral transform logic suitable for a Tool, script or command handler. */
class SceneTransformToolLogic {
public:
    /** @brief Query transform capability and build a transform operation. */
    EditorResult<DomainOperation> plan(IEditableTarget& target, const ObjectId& object,
                                       const SceneTransformValue& transform) const;
};

/** @brief Property adapter exposing scene TRS to generic inspector presenters. */
class ScenePropertyProvider final : public IPropertyProvider {
public:
    explicit ScenePropertyProvider(const SceneTargetBase* target) : target_(target) {}

    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;

private:
    const SceneTargetBase* target_ = nullptr;
};

}  // namespace eve::editor
