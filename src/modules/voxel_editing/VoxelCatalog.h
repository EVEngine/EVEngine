#pragma once

/**
 * @file VoxelCatalog.h
 * @brief MagicaVoxel-style sculpted models: bounded occupancy grids and hull sockets.
 */

#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::voxel_editing {

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

/** @brief Inclusive maximum MagicaVoxel-like object edge (cells). */
inline constexpr int kVoxelModelMaxSize = 32;
/** @brief Occupied-cell budget for one sculpted object. */
inline constexpr int kVoxelModelMaxOccupied = 4096;

/** @brief Polarity of one authored join socket. */
enum class VoxelSocketKind { None, Symmetric, Male, Female };

/** @brief Derived occupancy of a sculpted object. */
enum class VoxelCellFill { Empty, Partial, Filled };

/** @brief One face socket: matching tag plus compatible polarity. */
struct VoxelSocket {
    std::string     tag;
    VoxelSocketKind kind = VoxelSocketKind::None;
};

/** @brief One occupied cell inside a sculpted model. */
struct VoxelCoord {
    int x = 0;
    int y = 0;
    int z = 0;
};

/** @brief One MagicaVoxel-style object: a bounded occupancy grid. */
struct VoxelModelValue {
    ObjectId                   id;
    std::string                name;
    int                        sizeX = 8;
    int                        sizeY = 8;
    int                        sizeZ = 8;
    std::array<VoxelSocket, 6> sockets{};
    std::vector<VoxelCoord>    voxels;
};

/** @brief DDA pick against a sculpted model. */
struct VoxelPick {
    bool hit       = false;
    bool canAttach = false;
    int  hitX      = 0;
    int  hitY      = 0;
    int  hitZ      = 0;
    int  prevX     = 0;
    int  prevY     = 0;
    int  prevZ     = 0;
};

/**
 * @brief Parse a socket kind name.
 * @return Kind, or None when the name is empty/unknown.
 */
[[nodiscard]] VoxelSocketKind voxelSocketKindFromName(std::string_view name);

/**
 * @brief Stable serialized name for a socket kind.
 * @ownership Observed static string; callers must not free it.
 * @lifetime Valid for the process lifetime.
 * @thread Any.
 */
[[nodiscard]] const char* voxelSocketKindName(VoxelSocketKind kind);

/** @brief True when two facing sockets may join. */
[[nodiscard]] bool canJoinVoxelSockets(const VoxelSocket& a, const VoxelSocket& b);

/** @brief Opposite FaceDir index in PosX/NegX/PosY/NegY/PosZ/NegZ order. */
[[nodiscard]] int voxelOppositeFace(int face);

/** @brief Classify a sculpted model as empty, partial, or a solid cube. */
[[nodiscard]] VoxelCellFill voxelClassifyModelFill(const VoxelModelValue& model);

/** @brief True when @p model contains an occupied cell at (x,y,z). */
[[nodiscard]] bool isVoxelModelOccupied(const VoxelModelValue& model, int x, int y, int z);

/**
 * @brief Raycast occupied cells with MagicaVoxel-style previous-cell attach.
 * @param maxDistance Maximum travel along the normalized ray.
 */
[[nodiscard]] VoxelPick pickVoxelModel(const VoxelModelValue& model, float ox, float oy, float oz, float dx, float dy,
                                       float dz, float maxDistance);

/** @brief Revisioned project of MagicaVoxel-style sculpted models. */
class VoxelCatalogTarget final : public virtual IEditableTarget,
                                 public IDomainOperationTarget,
                                 public IDomainOperationTargetStaging,
                                 public IPropertyProvider,
                                 public IEditingSnapshotProvider {
public:
    explicit VoxelCatalogTarget(std::string id);

    static CapabilityId propertyCapabilityId() { return CapabilityId("eve.editor.target.voxel-catalog-properties"); }

    TargetId      targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion    dirtyRegion() const override { return dirty_; }
    void          clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;

    /**
     * @brief Query Inspector property and snapshot capabilities.
     * @return Borrowed pointer owned by this target, or null when unsupported.
     * @lifetime Valid until this target is destroyed or replaced by commitDomainState.
     * @thread Owner-thread only.
     */
    void* queryCapability(const CapabilityId&) override;

    EditorResult<void>                      applyDomainOperation(const DomainOperation&) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void>                      commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    eve::Result<eve::Revision>              currentRevision(const SelectionSnapshot&) const override;
    PropertySchema                          schema(const SelectionSnapshot&) const override;
    PropertyReadResult                      read(const SelectionSnapshot&, const PropertyPath&) const override;
    EditorResult<DomainOperation>           makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                                    PropertySetMode) const override;
    EditorResult<DomainOperation>           makeReset(const SelectionSnapshot&, const PropertyPath&) const override;

    [[nodiscard]] EditorResult<DomainOperation> makeCreateModel(const VoxelModelValue&) const;
    [[nodiscard]] EditorResult<DomainOperation> makeDeleteModel(const ObjectId&) const;
    [[nodiscard]] EditorResult<DomainOperation> makeSetVoxel(const ObjectId& model, int x, int y, int z,
                                                             bool occupied) const;

    const std::vector<VoxelModelValue>& models() const { return models_; }
    /**
     * @brief Look up a sculpted model by stable id.
     * @return Borrowed model owned by this catalog, or null when absent.
     * @ownership Borrowed from this target; callers must not delete it.
     * @lifetime Valid until the next mutation or this target is destroyed.
     * @thread Owner-thread only.
     */
    const VoxelModelValue* findModel(const ObjectId&) const;

    std::vector<EditorDiagnostic> validate() const;
    EditorValue                   snapshotValue() const override;
    EditorResult<void>            loadSnapshot(const EditorValue&);

    [[nodiscard]] std::vector<ObjectId> hullJoinPartners(const ObjectId& model, int face) const;

private:
    bool                          matches(const SelectionSnapshot&) const;
    EditorValue                   contentValue() const;
    EditorResult<DomainOperation> replacement(EditorValue, std::string = {}) const;
    /**
     * @brief Mutable model lookup used by occupancy edits.
     * @ownership Borrowed from this target; callers must not delete it.
     * @lifetime Valid until the next mutation or this target is destroyed.
     * @thread Owner-thread only.
     */
    VoxelModelValue* findModelMut(const ObjectId&);

    std::string                  id_;
    Revision                     revision_ = 1;
    EditRegion                   dirty_;
    std::vector<VoxelModelValue> models_;
};

}  // namespace eve::voxel_editing
