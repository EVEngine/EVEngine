#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorTargetV2.h"

#include <map>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Minimal serializable transform used by scene editing capabilities. */
struct SceneTransformValue {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    auto operator<=>(const SceneTransformValue&) const = default;
};

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
};

/** @brief Stable transform capability implemented by document and runtime targets. */
class ITransformEditTarget {
public:
    virtual ~ITransformEditTarget() = default;
    /** @brief Stable capability identity used instead of cross-module RTTI. */
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.transform"); }
    /** @brief Read one immutable transform value. */
    virtual EditorResult<SceneTransformValue> readTransform(const ObjectId& id) const = 0;
    /** @brief Build a reversible transform operation without applying it. */
    virtual EditorResult<DomainOperation> makeSetTransform(const ObjectId&            id,
                                                           const SceneTransformValue& transform) const = 0;
};

/**
 * @brief Shared in-memory scene target implementation for document/runtime adapters.
 *
 * Concrete subclasses differ only in host-facing target type. The mutation
 * protocol and capabilities stay identical so tools contain no backend branch.
 */
class SceneTargetBase : public IEditableTargetV2,
                        public IDomainOperationTarget,
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

    EditorResult<SceneObjectSnapshot> sceneObject(const ObjectId& id) const override;
    std::vector<ObjectId>             sceneChildren(const ObjectId& parent) const override;
    EditorResult<DomainOperation>     makeCreate(const CreateSceneObjectRequest& request) const override;
    EditorResult<SceneTransformValue> readTransform(const ObjectId& id) const override;
    EditorResult<DomainOperation>     makeSetTransform(const ObjectId&            id,
                                                       const SceneTransformValue& transform) const override;
    /** @brief Capture deterministic scene content for document persistence. */
    EditorValue snapshotValue() const;

private:
    static EditorValue                       transformValue(const SceneTransformValue& transform);
    static EditorResult<SceneTransformValue> parseTransform(const EditorValue& value);
    static EditorResult<SceneObjectSnapshot> parseObject(const EditorValue& value);
    static EditorValue                       objectValue(const SceneObjectSnapshot& object);

    std::string                             id_;
    std::string                             type_;
    unsigned long long                      revision_ = 0;
    EditRegion                              dirty_;
    std::map<ObjectId, SceneObjectSnapshot> objects_;
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

/** @brief Backend-neutral placement logic suitable for a Tool, script or command handler. */
class ScenePlacementToolLogic {
public:
    /** @brief Query hierarchy capability and build a create operation. */
    EditorResult<DomainOperation> plan(IEditableTargetV2& target, const CreateSceneObjectRequest& request) const;
};

/** @brief Backend-neutral transform logic suitable for a Tool, script or command handler. */
class SceneTransformToolLogic {
public:
    /** @brief Query transform capability and build a transform operation. */
    EditorResult<DomainOperation> plan(IEditableTargetV2& target, const ObjectId& object,
                                       const SceneTransformValue& transform) const;
};

}  // namespace eve::editor
