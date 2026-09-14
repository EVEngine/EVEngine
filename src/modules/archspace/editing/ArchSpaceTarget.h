#pragma once

#include "archspace/ArchSpaceDocument.h"
#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingGizmo.h"
#include "editing/EditingProperty.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::archspace_editing {

using CapabilityId       = editing::CapabilityId;
using DiagnosticSeverity = editing::DiagnosticSeverity;
using DomainOperation    = editing::DomainOperation;
using EditRegion         = editing::EditRegion;
using EditorDiagnostic   = editing::Diagnostic;
template <class T>
using EditorResult                  = editing::Result<T>;
using EditorStatus                  = editing::Status;
using EditorValue                   = editing::Value;
using IDomainOperationTarget        = editing::IDomainOperationTarget;
using IDomainOperationTargetStaging = editing::IDomainOperationTargetStaging;
using IEditableTarget               = editing::IEditableTarget;
using IEditingSnapshotProvider      = editing::IEditingSnapshotProvider;
using IPropertyProvider             = editing::IPropertyProvider;
using ObjectId                      = editing::ObjectId;
using PropertyDescriptor            = editing::PropertyDescriptor;
using PropertyFlag                  = editing::PropertyFlag;
using PropertyPath                  = editing::PropertyPath;
using PropertyReadResult            = editing::PropertyReadResult;
using PropertyReadState             = editing::PropertyReadState;
using PropertySchema                = editing::PropertySchema;
using PropertySetMode               = editing::PropertySetMode;
using PropertyType                  = editing::PropertyType;
using Revision                      = editing::Revision;
using RuleId                        = editing::RuleId;
using SelectionSnapshot             = editing::SelectionSnapshot;
using TargetDescriptor              = editing::TargetDescriptor;
using TargetId                      = editing::TargetId;
using EditorGizmoSnapshot           = editing::GizmoSnapshot;
using EditorGizmoPrimitive          = editing::GizmoPrimitive;
using editing::validatePropertyValue;

/**
 * @brief Revisioned ArchSpace document target for Inspector, tools and automation.
 *
 * Mutations are expressed as reversible `archspace.document.replace.v1` domain
 * operations. High-level helpers (bootstrap/room/opening/item) plan those
 * replacements without bypassing validation.
 */
class ArchSpaceDocumentTarget final : public virtual IEditableTarget,
                                      public IDomainOperationTarget,
                                      public IDomainOperationTargetStaging,
                                      public IPropertyProvider,
                                      public IEditingSnapshotProvider {
public:
    explicit ArchSpaceDocumentTarget(std::string id);

    /** @brief Capability identity published by describe() for Inspector property editing. */
    static CapabilityId propertyCapabilityId() { return CapabilityId("eve.editor.target.archspace-properties"); }

    TargetId         targetId() const override { return TargetId(id_); }
    std::uint64_t    revision() const override { return revision_; }
    EditRegion       dirtyRegion() const override { return dirty_; }
    void             clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /**
     * @brief Query Inspector property capability.
     * @ownership Borrowed from this target; callers must not delete it.
     * @lifetime Valid until this target is destroyed.
     */
    void*                                   queryCapability(const CapabilityId& capability) override;
    EditorResult<void>                      applyDomainOperation(const DomainOperation& operation) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void>            commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    eve::Result<eve::Revision>    currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema                schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult            read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override;

    /** @brief Plan site→building→level creation on an empty document. */
    EditorResult<DomainOperation> makeBootstrap(const std::string& siteId, const std::string& buildingId,
                                                const std::string& levelId, double levelHeight = 3.0) const;
    /** @brief Plan a rectangular room (four walls + slab + zone) under a level. */
    EditorResult<DomainOperation> makeCreateRectRoom(const std::string& levelId, const std::string& roomId,
                                                     std::string roomName, double originX, double originZ, double sizeX,
                                                     double sizeZ, double wallHeight = 3.0, double wallThickness = 0.2,
                                                     double slabThickness = 0.2) const;
    /** @brief Plan a single wall under a level. */
    EditorResult<DomainOperation> makeCreateWall(const std::string& levelId, const std::string& wallId,
                                                 std::string wallName, archspace::Vec2 start, archspace::Vec2 end,
                                                 double height = 3.0, double thickness = 0.2) const;
    /** @brief Plan a door/window opening on an existing wall. */
    EditorResult<DomainOperation> makeCreateOpening(const std::string& wallId, const std::string& openingId,
                                                    archspace::OpeningKind kind, double t, double width, double height,
                                                    double sill = 0.0) const;
    /** @brief Plan a catalog item placement on a level. */
    EditorResult<DomainOperation> makePlaceItem(const std::string& levelId, const std::string& itemId,
                                                std::string catalogId, archspace::Vec3 position,
                                                double yawDegrees = 0.0) const;
    /** @brief Plan cascading deletion of one node. */
    EditorResult<DomainOperation> makeDeleteNode(const ObjectId& id) const;

    [[nodiscard]] const archspace::Document&    document() const { return document_; }
    [[nodiscard]] std::vector<EditorDiagnostic> validate() const;
    [[nodiscard]] EditorValue                   snapshotValue() const override;
    EditorResult<void>                          loadSnapshot(const EditorValue& snapshot);
    /** @brief Build a revision-bound overlay for walls, zones, items and openings. */
    [[nodiscard]] EditorResult<EditorGizmoSnapshot> gizmo() const;
    /** @brief Bake renderer-neutral triangle mesh for the current revision. */
    [[nodiscard]] archspace::MeshBake bakeMesh() const { return document_.bakeMesh(); }

private:
    bool                          matches(const SelectionSnapshot& selection) const;
    EditorValue                   contentValue() const;
    EditorResult<DomainOperation> replacement(EditorValue content, std::string property = {}) const;
    EditorResult<void>            installContent(const EditorValue& content);
    /**
     * @brief Resolve the single selected ArchSpace node for property edits.
     * @ownership Non-owning pointer into document_; do not free.
     * @lifetime Until the next mutating call on this target.
     */
    const archspace::Node* selectedNode(const SelectionSnapshot& selection) const;

    std::string         id_;
    Revision            revision_ = 1;
    EditRegion          dirty_;
    archspace::Document document_;
};

}  // namespace eve::archspace_editing
