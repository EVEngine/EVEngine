#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editor/EditorTargetV2.h"

#include <map>
#include <string>
#include <vector>

namespace eve::editor {

class IPhysicsColliderAssetResolver;

/** @brief Serializable, backend-neutral 2D/3D collider authoring target. */
class PhysicsColliderTarget final : public IEditableTargetV2,
                                    public IDomainOperationTarget,
                                    public IDomainOperationTargetStaging,
                                    public IPropertyProvider {
public:
    explicit PhysicsColliderTarget(std::string id, int dimensions = 3);

    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;

    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;

    /** @brief Capture deterministic collider content for asset/scene persistence. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a versioned collider snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
    /** @brief Report cross-property shape/body problems for inspector overlays. */
    std::vector<EditorDiagnostic> validate() const;

private:
    PropertySchema colliderSchema() const;
    std::map<std::string, EditorValue> defaults() const;
    EditorResult<void> validateAssignment(const PropertyDescriptor& descriptor,
                                          const EditorValue& value) const;
    bool selectionMatches(const SelectionSnapshot& selection) const;

    std::string id_;
    int dimensions_ = 3;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
    std::map<std::string, EditorValue> values_;
};

/** @brief Atomic runtime publication boundary for a complete collider candidate. */
class IPhysicsColliderRuntimeSink {
public:
    virtual ~IPhysicsColliderRuntimeSink() = default;
    /** @brief Publish a candidate; failure must preserve the previous live collider. */
    virtual EditorResult<void> publish(const PhysicsColliderTarget& candidate) = 0;
};

/** @brief Candidate-first collider target whose commit/undo publishes to a live sink. */
class PhysicsColliderPublishingTarget final : public IDomainOperationTarget,
                                               public IDomainOperationTargetStaging {
public:
    /** @brief Create an owned collider document bound to a non-owning runtime sink. */
    PhysicsColliderPublishingTarget(std::string id, int dimensions,
                                     IPhysicsColliderRuntimeSink* sink);
    const std::string& targetId() const override { return document_.targetId(); }
    unsigned long long revision() const override { return document_.revision(); }
    EditRegion dirtyRegion() const override { return document_.dirtyRegion(); }
    void clearDirtyRegion() override { document_.clearDirtyRegion(); }
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Mutable authoring target used by the component property binding. */
    PhysicsColliderTarget& authoringTarget() { return document_; }
    /** @brief Immutable authoring target used by diagnostics and previews. */
    const PhysicsColliderTarget& authoringTarget() const { return document_; }

private:
    PhysicsColliderTarget document_;
    IPhysicsColliderRuntimeSink* sink_ = nullptr;
    bool staging_ = false;
};

/** @brief Serializable joint authoring target using stable body references. */
class PhysicsJointTarget final : public IEditableTargetV2,
                                 public IDomainOperationTarget,
                                 public IDomainOperationTargetStaging,
                                 public IPropertyProvider {
public:
    explicit PhysicsJointTarget(std::string id);

    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override;
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;
    /** @brief Capture deterministic joint content. */
    EditorValue snapshotValue() const;
    /** @brief Report missing bodies, invalid axes and limit ranges. */
    std::vector<EditorDiagnostic> validate() const;

private:
    static PropertySchema jointSchema();
    static std::map<std::string, EditorValue> defaults();
    bool selectionMatches(const SelectionSnapshot& selection) const;

    std::string id_;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
    std::map<std::string, EditorValue> values_;
};

}  // namespace eve::editor

namespace eve::physics {
class Body;
class Body3D;
class Fixture;
class Shape3D;
}

namespace eve::editor {

/** @brief Optional bridge creating primitive Box2D/Box3D shapes from collider documents. */
class PhysicsColliderRuntimeBuilder {
public:
    /** @brief Create a box/circle Fixture owned by the supplied 2D body. */
    EditorResult<physics::Fixture*> build2D(const PhysicsColliderTarget& target,
                                            physics::Body* body) const;
    /** @brief Create a primitive or resolved polygon/chain Fixture. */
    EditorResult<physics::Fixture*> build2D(const PhysicsColliderTarget& target,
                                            physics::Body* body,
                                            const IPhysicsColliderAssetResolver& assets) const;
    /** @brief Create a box/sphere/capsule Shape3D owned by the supplied 3D body. */
    EditorResult<physics::Shape3D*> build3D(const PhysicsColliderTarget& target,
                                            physics::Body3D* body) const;
    /** @brief Create a primitive or resolved complex 3D collider. */
    EditorResult<physics::Shape3D*> build3D(const PhysicsColliderTarget& target,
                                            physics::Body3D* body,
                                            const IPhysicsColliderAssetResolver& assets) const;
};

/** @brief Candidate-first Shape3D replacement sink for one borrowed live body. */
class PhysicsCollider3DRuntimeSink final : public IPhysicsColliderRuntimeSink {
public:
    /**
     * @brief Bind a body, optional current shape, and optional complex-asset resolver.
     * @remarks The body and resolver must outlive the sink. The sink does not
     *          destroy its current shape on destruction; World3D retains ownership.
     */
    PhysicsCollider3DRuntimeSink(physics::Body3D* body, physics::Shape3D* current = nullptr,
                                 const IPhysicsColliderAssetResolver* assets = nullptr)
        : body_(body), current_(current), assets_(assets) {}
    EditorResult<void> publish(const PhysicsColliderTarget& candidate) override;
    /** @brief Return the current borrowed shape after the latest successful swap. */
    physics::Shape3D* shape() const { return current_; }

private:
    physics::Body3D* body_ = nullptr;
    physics::Shape3D* current_ = nullptr;
    const IPhysicsColliderAssetResolver* assets_ = nullptr;
};

/** @brief Candidate-first Fixture replacement sink for one borrowed live 2D body. */
class PhysicsCollider2DRuntimeSink final : public IPhysicsColliderRuntimeSink {
public:
    /** @brief Bind a body, optional current fixture, and optional polygon/chain resolver. */
    PhysicsCollider2DRuntimeSink(physics::Body* body, physics::Fixture* current = nullptr,
                                 const IPhysicsColliderAssetResolver* assets = nullptr)
        : body_(body), current_(current), assets_(assets) {}
    EditorResult<void> publish(const PhysicsColliderTarget& candidate) override;
    /** @brief Return the current borrowed fixture after the latest successful swap. */
    physics::Fixture* fixture() const { return current_; }

private:
    physics::Body* body_ = nullptr;
    physics::Fixture* current_ = nullptr;
    const IPhysicsColliderAssetResolver* assets_ = nullptr;
};

}  // namespace eve::editor
