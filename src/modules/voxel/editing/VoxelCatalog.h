#pragma once
#include "common/Export.h"


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
[[nodiscard]] EVENGINE_API_ORCHESTRATION const char* voxelSocketKindName(VoxelSocketKind kind);

/** @brief True when two facing sockets may join. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION bool canJoinVoxelSockets(const VoxelSocket& a, const VoxelSocket& b);

/** @brief Opposite FaceDir index in PosX/NegX/PosY/NegY/PosZ/NegZ order. */
[[nodiscard]] int voxelOppositeFace(int face);

/** @brief Classify a sculpted model as empty, partial, or a solid cube. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION VoxelCellFill voxelClassifyModelFill(const VoxelModelValue& model);

/** @brief True when @p model contains an occupied cell at (x,y,z). */
[[nodiscard]] bool isVoxelModelOccupied(const VoxelModelValue& model, int x, int y, int z);

/**
 * @brief Raycast occupied cells with MagicaVoxel-style previous-cell attach.
 * @param maxDistance Maximum travel along the normalized ray.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION VoxelPick pickVoxelModel(const VoxelModelValue& model, float ox, float oy,
                                                                  float oz, float dx, float dy, float dz,
                                                                  float maxDistance);

/** @brief Revisioned project of MagicaVoxel-style sculpted models. */
class EVENGINE_API_ORCHESTRATION VoxelCatalogTarget final : public ::eve::editing::EditableTargetState,
                                                            public virtual IEditableTarget,
                                                            public IDomainOperationTarget,
                                                            public IDomainOperationTargetStaging,
                                                            public IPropertyProvider,
                                                            public IEditingSnapshotProvider {
public:
    /** @brief Voxel catalog target. */
    explicit VoxelCatalogTarget(std::string id);

    /** @brief Property capability id. */
    static CapabilityId propertyCapabilityId() { return CapabilityId("eve.editor.target.voxel-catalog-properties"); }

    /** @brief Target id. */
    TargetId         targetId() const override { return TargetId(id_); }
    /** @brief Describe. */
    TargetDescriptor describe() const override;

    /**
     * @brief Query Inspector property and snapshot capabilities.
     * @return Borrowed pointer owned by this target, or null when unsupported.
     * @lifetime Valid until this target is destroyed or replaced by commitDomainState.
     * @thread Owner-thread only.
     */
    void* queryCapability(const CapabilityId&) override;

    /** @brief Applies domain operation. */
    EditorResult<void>                      applyDomainOperation(const DomainOperation&) override;
    /** @brief Clones domain state. */
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Commits domain state. */
    EditorResult<void>                      commitDomainState(std::unique_ptr<IDomainOperationTarget>) override;
    /** @brief Current revision. */
    eve::Result<eve::Revision>              currentRevision(const SelectionSnapshot&) const override;
    /** @brief Schema. */
    PropertySchema                          schema(const SelectionSnapshot&) const override;
    /** @brief Reads . */
    PropertyReadResult                      read(const SelectionSnapshot&, const PropertyPath&) const override;
    /** @brief Make set. */
    EditorResult<DomainOperation>           makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                                    PropertySetMode) const override;
    /** @brief Make reset. */
    EditorResult<DomainOperation>           makeReset(const SelectionSnapshot&, const PropertyPath&) const override;

    /** @brief Make create model. */
    [[nodiscard]] EditorResult<DomainOperation> makeCreateModel(const VoxelModelValue&) const;
    /** @brief Make delete model. */
    [[nodiscard]] EditorResult<DomainOperation> makeDeleteModel(const ObjectId&) const;
    /** @brief Make set voxel. */
    [[nodiscard]] EditorResult<DomainOperation> makeSetVoxel(const ObjectId& model, int x, int y, int z,
                                                             bool occupied) const;

    /** @brief Models. */
    const std::vector<VoxelModelValue>& models() const { return models_; }
    /**
     * @brief Look up a sculpted model by stable id.
     * @return Borrowed model owned by this catalog, or null when absent.
     * @ownership Borrowed from this target; callers must not delete it.
     * @lifetime Valid until the next mutation or this target is destroyed.
     * @thread Owner-thread only.
     */
    const VoxelModelValue* findModel(const ObjectId&) const;

    /** @brief Validate. */
    std::vector<EditorDiagnostic> validate() const;
    /** @brief Snapshot value. */
    EditorValue                   snapshotValue() const override;
    /** @brief Loads snapshot. */
    EditorResult<void>            loadSnapshot(const EditorValue&);

    /** @brief Hull join partners. */
    [[nodiscard]] std::vector<ObjectId> hullJoinPartners(const ObjectId& model, int face) const;

private:
    /** @brief Matches. */
    bool                          matches(const SelectionSnapshot&) const;
    /** @brief Content value. */
    EditorValue                   contentValue() const;
    /** @brief Replacement. */
    EditorResult<DomainOperation> replacement(EditorValue, std::string = {}) const;
    /**
     * @brief Mutable model lookup used by occupancy edits.
     * @ownership Borrowed from this target; callers must not delete it.
     * @lifetime Valid until the next mutation or this target is destroyed.
     * @thread Owner-thread only.
     */
    VoxelModelValue* findModelMut(const ObjectId&);

    std::string                  id_;
    std::vector<VoxelModelValue> models_;
};

}  // namespace eve::voxel_editing
