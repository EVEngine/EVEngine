#pragma once
#include "common/Export.h"


#include "editing/EditingProperty.h"
#include "editing/EditingTargetOperations.h"
#include "editing/EditableTarget.h"
#include "physics/editing/PhysicsColliderAsset.h"

#include <map>
#include <string>
#include <vector>

namespace eve::physics_editing {

using editing::CapabilityId;
using editing::Diagnostic;
using editing::DiagnosticSeverity;
using editing::DomainOperation;
using editing::EditRegion;
using editing::IDomainOperationTarget;
using editing::IDomainOperationTargetStaging;
using editing::IEditableTarget;
using editing::IPropertyProvider;
using editing::PropertyDescriptor;
using editing::PropertyFlag;
using editing::PropertyPath;
using editing::PropertyReadResult;
using editing::PropertyReadState;
using editing::PropertySchema;
using editing::PropertySetMode;
using editing::PropertyType;
using editing::Result;
using editing::RuleId;
using editing::SelectionSnapshot;
using editing::Status;
using editing::TargetDescriptor;
using editing::TargetId;
using editing::Value;

using EditorStatus     = editing::Status;
using EditorValue      = editing::Value;
using EditorDiagnostic = editing::Diagnostic;

using IPhysicsColliderAssetResolver = eve::physics_editing::IPhysicsColliderAssetResolver;

/** @brief Serializable, backend-neutral 2D/3D collider authoring target. */
class EVENGINE_API_DOMAINS PhysicsColliderTarget final : public ::eve::editing::EditableTargetState,
                                                         public virtual IEditableTarget,
                                                         public IDomainOperationTarget,
                                                         public IDomainOperationTargetStaging,
                                                         public IPropertyProvider {
public:
    /** @brief Physics collider target. */
    explicit PhysicsColliderTarget(std::string id, int dimensions = 3);

    /** @brief Target id. */
    TargetId           targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor   describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime
     * Valid until this target is destroyed or mutated. */
    void*              queryCapability(const CapabilityId& capability) override;
    /** @brief Applies domain operation. */
    Result<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    [[nodiscard]] Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;

    /** @brief Current revision. */
    eve::Result<eve::Revision>    currentRevision(const SelectionSnapshot& selection) const override;
    /** @brief Schema. */
    PropertySchema                schema(const SelectionSnapshot& selection) const override;
    /** @brief Reads . */
    PropertyReadResult            read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    /** @brief Make set. */
    Result<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    /** @brief Make reset. */
    Result<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override;

    /** @brief Capture deterministic collider content for asset/scene persistence. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a versioned collider snapshot. */
    Result<void> loadSnapshot(const EditorValue& snapshot);
    /** @brief Report cross-property shape/body problems for inspector overlays. */
    std::vector<EditorDiagnostic> validate() const;

private:
    /** @brief Collider schema. */
    PropertySchema                     colliderSchema() const;
    std::map<std::string, EditorValue> defaults() const;
    /** @brief Validate assignment. */
    Result<void> validateAssignment(const PropertyDescriptor& descriptor, const EditorValue& value) const;
    /** @brief Selection matches. */
    bool               selectionMatches(const SelectionSnapshot& selection) const;

    std::string                        id_;
    int                                dimensions_ = 3;
    std::map<std::string, EditorValue> values_;
};

/** @brief Atomic runtime publication boundary for a complete collider candidate. */
class IPhysicsColliderRuntimeSink {
public:
    /** @brief Releases IPhysicsColliderRuntimeSink resources. */
    virtual ~IPhysicsColliderRuntimeSink() = default;
    /** @brief Publish a candidate; failure must preserve the previous live collider. */
    virtual Result<void> publish(const PhysicsColliderTarget& candidate) = 0;
};

/** @brief Candidate-first collider target whose commit/undo publishes to a live sink. */
class EVENGINE_API_DOMAINS PhysicsColliderPublishingTarget final : public IDomainOperationTarget,
                                                                   public IDomainOperationTargetStaging {
public:
    /** @brief Create an owned collider document bound to a non-owning runtime sink. */
    PhysicsColliderPublishingTarget(std::string id, int dimensions, IPhysicsColliderRuntimeSink* sink);
    /** @brief Target id. */
    TargetId targetId() const override { return TargetId(document_.targetId()); }
    /** @brief Revision. */
    std::uint64_t revision() const override { return document_.revision(); }
    /** @brief Dirty region. */
    EditRegion         dirtyRegion() const override { return document_.dirtyRegion(); }
    /** @brief Clears dirty region. */
    void               clearDirtyRegion() override { document_.clearDirtyRegion(); }
    /** @brief Applies domain operation. */
    Result<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    [[nodiscard]] Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Mutable authoring target used by the component property binding. */
    PhysicsColliderTarget& authoringTarget() { return document_; }
    /** @brief Immutable authoring target used by diagnostics and previews. */
    const PhysicsColliderTarget& authoringTarget() const { return document_; }

private:
    PhysicsColliderTarget        document_;
    IPhysicsColliderRuntimeSink* sink_    = nullptr;
    bool                         staging_ = false;
};

/** @brief Serializable joint authoring target using stable body references. */
class EVENGINE_API_DOMAINS PhysicsJointTarget final : public ::eve::editing::EditableTargetState,
                                                      public virtual IEditableTarget,
                                                      public IDomainOperationTarget,
                                                      public IDomainOperationTargetStaging,
                                                      public IPropertyProvider {
public:
    /** @brief Physics joint target. */
    explicit PhysicsJointTarget(std::string id);

    /** @brief Target id. */
    TargetId           targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor   describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime
     * Valid until this target is destroyed or mutated. */
    void*              queryCapability(const CapabilityId& capability) override;
    /** @brief Applies domain operation. */
    Result<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clones domain state. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    [[nodiscard]] Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision>       currentRevision(const SelectionSnapshot& selection) const override;
    /** @brief Schema. */
    PropertySchema                   schema(const SelectionSnapshot& selection) const override;
    /** @brief Reads . */
    PropertyReadResult               read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    /** @brief Make set. */
    Result<DomainOperation>    makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                             const EditorValue& value, PropertySetMode mode) const override;
    /** @brief Make reset. */
    Result<DomainOperation>    makeReset(const SelectionSnapshot& selection,
                                               const PropertyPath&      path) const override;
    /** @brief Capture deterministic joint content. */
    EditorValue snapshotValue() const;
    /** @brief Report missing bodies, invalid axes and limit ranges. */
    std::vector<EditorDiagnostic> validate() const;

private:
    /** @brief Joint schema. */
    static PropertySchema                     jointSchema();
    static std::map<std::string, EditorValue> defaults();
    /** @brief Selection matches. */
    bool                                      selectionMatches(const SelectionSnapshot& selection) const;

    std::string                        id_;
    std::map<std::string, EditorValue> values_;
};

}  // namespace eve::physics_editing

namespace eve::physics {
class Body;
class Body3D;
class Fixture;
class Shape3D;
}  // namespace eve::physics

namespace eve::physics_editing {

/** @brief Optional bridge creating primitive Box2D/Box3D shapes from collider documents. */
class PhysicsColliderRuntimeBuilder {
public:
    /** @brief Create a box/circle Fixture owned by the supplied 2D body. */
    Result<physics::Fixture*> build2D(const PhysicsColliderTarget& target, physics::Body* body) const;
    /** @brief Create a primitive or resolved polygon/chain Fixture. */
    Result<physics::Fixture*> build2D(const PhysicsColliderTarget& target, physics::Body* body,
                                            const IPhysicsColliderAssetResolver& assets) const;
    /** @brief Create a box/sphere/capsule Shape3D owned by the supplied 3D body. */
    Result<physics::Shape3D*> build3D(const PhysicsColliderTarget& target, physics::Body3D* body) const;
    /** @brief Create a primitive or resolved complex 3D collider. */
    Result<physics::Shape3D*> build3D(const PhysicsColliderTarget& target, physics::Body3D* body,
                                            const IPhysicsColliderAssetResolver& assets) const;
};

/** @brief Candidate-first Shape3D replacement sink for one borrowed live body. */
class EVENGINE_API_DOMAINS PhysicsCollider3DRuntimeSink final : public IPhysicsColliderRuntimeSink {
public:
    /**
     * @brief Bind a body, optional current shape, and optional complex-asset resolver.
     * @remarks The body and resolver must outlive the sink. The sink does not
     *          destroy its current shape on destruction; World3D retains ownership.
     */
    PhysicsCollider3DRuntimeSink(physics::Body3D* body, physics::Shape3D* current = nullptr,
                                 const IPhysicsColliderAssetResolver* assets = nullptr)
        /** @brief Body. */
        : body_(body), current_(current), assets_(assets) {}
    /** @brief Publish. */
    Result<void> publish(const PhysicsColliderTarget& candidate) override;
    /** @brief Return the current borrowed shape after the latest successful swap. @return Borrowed pointer owned by the
     * physics world. @lifetime Valid until the next publish, world mutation, or runtime destruction. */
    physics::Shape3D* shape() const { return current_; }

private:
    physics::Body3D*                     body_    = nullptr;
    physics::Shape3D*                    current_ = nullptr;
    const IPhysicsColliderAssetResolver* assets_  = nullptr;
};

/** @brief Candidate-first Fixture replacement sink for one borrowed live 2D body. */
class EVENGINE_API_DOMAINS PhysicsCollider2DRuntimeSink final : public IPhysicsColliderRuntimeSink {
public:
    /** @brief Bind a body, optional current fixture, and optional polygon/chain resolver. */
    PhysicsCollider2DRuntimeSink(physics::Body* body, physics::Fixture* current = nullptr,
                                 const IPhysicsColliderAssetResolver* assets = nullptr)
        /** @brief Body. */
        : body_(body), current_(current), assets_(assets) {}
    /** @brief Publish. */
    Result<void> publish(const PhysicsColliderTarget& candidate) override;
    /** @brief Return the current borrowed fixture after the latest successful swap. @return Borrowed pointer owned by
     * the physics world. @lifetime Valid until the next publish, world mutation, or runtime destruction. */
    physics::Fixture* fixture() const { return current_; }

private:
    physics::Body*                       body_    = nullptr;
    physics::Fixture*                    current_ = nullptr;
    const IPhysicsColliderAssetResolver* assets_  = nullptr;
};

}  // namespace eve::physics_editing
